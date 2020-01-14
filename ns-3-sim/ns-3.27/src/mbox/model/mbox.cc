/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Jinhui Song<jinhuis2@illinois.edu>
 */


#include "mbox.h"
// #include "ns3/apps.h"

using namespace std;

namespace ns3{

NS_LOG_COMPONENT_DEFINE ("MiddlePoliceBox");

/* ------------- Begin: implementation of LongRunSmoothManager, i.e. smoothing TCP peak-to-peak -------------- */
LongRunSmoothManager::LongRunSmoothManager (uint32_t N, double beta, uint32_t M): N(N), beta(beta), M(M), counter(1)
{
    lr_rwnd = vector<double> (N, 0);
    lr_cwnd = vector<double> (N, 0);
    rwnds = vector<list<double>> (N);
    cwnds = vector<list<double>> (N);
    lr_capacity = 0;
}

void
LongRunSmoothManager::updateWnd (vector<uint32_t> rwnd, vector<uint32_t> cwnd, double capacity)
{
    double st_beta = 0.08;          // cover only <20 intervals for the beginning

    // 1. exp moving average, not fast for real underusing
    for (uint32_t i = 0; i < N; i ++)
    {
        lr_rwnd[i] = !lr_rwnd[i]? rwnd[i] : 
                     capa_wnd.size() < M? (1 - st_beta) * lr_rwnd[i] + st_beta * rwnd[i] :
                     (1 - beta) * lr_rwnd[i] + beta * rwnd[i];
        lr_cwnd[i] = !lr_cwnd[i]? cwnd[i] :
                     capa_wnd.size() < M? (1 - st_beta) * lr_cwnd[i] + st_beta * cwnd[i] :
                     (1 - beta) * lr_cwnd[i] + beta * cwnd[i];
    }

    // 2. moving average
    // uint32_t wnd_size = (uint32_t) 1 / beta;
    // for(uint32_t i = 0; i < N; i ++)
    // {
    //     rwnds[i].push_back(rwnd[i]);
    //     if (rwnds[i].size() > wnd_size) rwnds[i].pop_front();
    //     cwnds[i].push_back(cwnd[i]);
    //     if (cwnds[i].size() > wnd_size) cwnds[i].pop_front();

    //      lr_rwnd[i] = !lr_rwnd[i]? rwnd[i] : 
    //                  capa_wnd.size() < M? (1 - st_beta) * lr_rwnd[i] + st_beta * rwnd[i] :
    //                  accumulate(rwnds[i].begin(), rwnds[i].end(), 0) / rwnds[i].size();
    //     lr_cwnd[i] = !lr_cwnd[i]? cwnd[i] :
    //                  capa_wnd.size() < M? (1 - st_beta) * lr_cwnd[i] + st_beta * cwnd[i] :
    //                  accumulate(cwnds[i].begin(), cwnds[i].end(), 0) / cwnds[i].size();
    // }

    if (capa_wnd.size() < 2) capacity = 50;         // for smooth beginning, and UDP overestimation
    capa_wnd.push_back(capacity);
    if (capa_wnd.size() > M) capa_wnd.pop_front();
    double max_capacity = *max_element(capa_wnd.begin(), capa_wnd.end());

    double delta = beta * M;
    if (counter ++ > M)
    {
        lr_capacity = lr_capacity? (1 - delta) * lr_capacity + delta * max_capacity : max_capacity;
        counter = 1;
    }
    else if (capa_wnd.size() < M)               // 1st M: just use the max for beginning
    {
        delta = 0.25;
        lr_capacity = lr_capacity? (1 - delta) * lr_capacity + delta * max_capacity : max_capacity;
    }

    NS_LOG_INFO ("LR capacity: " << lr_capacity << ", beta: " << beta << ", M: " << M << ", delta: " << delta);
}

void
LongRunSmoothManager::setWnd (vector<double> sm_rwnd, vector<double> sm_cwnd)
{
    lr_rwnd = sm_rwnd;      // be careful!
    lr_cwnd = sm_cwnd;
}

vector<double> 
LongRunSmoothManager::getRwnd()
{
    return lr_rwnd;
}

vector<double>
LongRunSmoothManager::getCwnd()
{
    return lr_cwnd;
}

double
LongRunSmoothManager::getCapacity()
{
    return lr_capacity;
}

double
LongRunSmoothManager::getCtrlCapacity()
{
    return lr_capacity * 0.75;      // for TCP flows
    // return lr_capacity;             // just for test
}

void
LongRunSmoothManager::logging()
{
    cout << "-- LRM: capacity: " << lr_capacity << endl;
    cout << "  Flow     Rwnd        Cwnd" << endl;
    for (uint32_t i = 0; i < N; i ++)
        cout << "  " << i << "        " << lr_rwnd[i] << "          " << lr_cwnd[i] << endl;
    cout << endl;
}

/* ------------- Begin: implementation of SlowstartManager, i.e. control for slow start ------------- */

SlowstartManager::SlowstartManager(vector<double> weight, uint32_t safe_Th):weight(weight), safe_Th(safe_Th)
{
    N = weight.size();
    last_dwnd = vector<uint32_t> (N, 0);
    isSlowstart = vector<bool> (N, true);
    ifOld = vector<bool> (N, false);
    slowDrop = vector<bool> (N, false);
}

vector<bool> SlowstartManager::refresh(vector<uint32_t> dwnd, uint32_t safe_count, vector<uint32_t> u_cnt, vector<bool> isSS, vector<bool> &if_end, vector<bool> &ssDrop, vector<double> weight, bool initial_safe)
{
    if(!weight.size()) this->weight = weight;
    uint32_t last_dsum = accumulate(last_dwnd.begin(), last_dwnd.end(), 0);
    uint32_t dsum = accumulate(dwnd.begin(), dwnd.end(), 0);
    // if (dsum >= 1.5 * last_dsum && ( safe_count > 50 || initial_safe) )     // high headroom
    if (safe_count > safe_Th || initial_safe)        // debug UDP mode
    {
        // cout << " -- Debug: dsum rapidly increase! (or safe count > 50, no drop since start.)" << endl;
        // cout << " -- dsum: " << dsum << ", last_dsum: " << last_dsum << ", safe count: " << safe_count << endl;
        last_dwnd = dwnd;
        ifOld = vector<bool> (N, false);
        return ifOld;
    }
    for (uint32_t i = 0; i < N; i ++)
    {
        // if(u_cnt[i] <= 1) isSlowstart[i] = false;                    // based on simulation, no need to manually set
        // if(last_dwnd[i] == 0 && dwnd[i] == 0) isSlowstart[i] = true;   // return slow start: maybe harsher 
        if (isSS[i]) isSlowstart[i] = true;
    }
    ssDrop = vector<bool>(N, false);
    for (uint32_t i = 0; i < N; i ++)
    {
        if(!isSlowstart[i]) ifOld[i] = true;                    // not SS
        else if(2 * dwnd[i] < 0.8 * this->weight[i] * dsum)     // SS with low rate: let it go
            ifOld[i] = false;
        else                                                    // SS with high rate: start control
        {
            if_end[i] = true;
            if(u_cnt[i] >= 1 ) ssDrop[i] = true;                // don't squeeze if link drop
            isSlowstart[i] = false;
            ifOld[i] = true;
        }
    }
    last_dwnd = dwnd;
    slowDrop = ssDrop;
    return ifOld;
}

vector<uint32_t> SlowstartManager::get_last_dwnd()
{
    return last_dwnd;
}

void SlowstartManager::print()
{
    cout << "    Drop? Ctrl?   Slow start?    last dwnd" << endl;
    for(uint32_t i = 0; i < N; i ++)
        cout << " " << i << "   " << slowDrop[i] << "   " << ifOld[i] << "        " << isSlowstart[i] << "              " << last_dwnd[i] << endl;
    cout << endl;

}

/* ------------- Begin: implementation of BandwidthManager, i.e. allocate cwnd adaptively ------------- */

void BandwidthManager::refresh(vector<double> sm_rwnd, double capacity)
{
    vector<double> tmp_cwnd(N, 0);
    vector<bool> under_user(N, false);
    this->capacity = capacity;
    for(uint32_t i = 0; i < N; i ++)
        tmp_cwnd[i] = weight[i] * capacity;
    wei_cwnd = tmp_cwnd;
    iter_realloc(sm_rwnd, tmp_cwnd, under_user);
    this->tmp_cwnd = tmp_cwnd;
    for(uint32_t i = 0; i < N; i ++)
        cur_cwnd[i] = max(weight[i] * capacity, tmp_cwnd[i]);
}

void BandwidthManager::iter_realloc(vector<double> &sm_rwnd, vector<double> &tmp_cwnd, vector<bool> &under_user)
{
    static int depth = 0;
    double surplus = 0.0, wsum = 0.0;
    for(uint32_t i = 0; i < N; i ++)
        if(tmp_cwnd[i] > (1 + margin) * sm_rwnd[i])     // if the flow under-uses BW
        {
            under_user[i] = true;
            surplus += tmp_cwnd[i] - (1 + margin) * sm_rwnd[i];
        }
    for(uint32_t i = 0; i < N; i ++)
        if(!under_user[i]) wsum += weight[i];
    if (wsum == 0) return;
    if(!surplus) return;

    uint32_t cnt = 0;
    for(uint32_t i = 0; i < N; i ++)
    {
        if(under_user[i]) 
            tmp_cwnd[i] = (1 + margin) * sm_rwnd[i];    // for under-user
        else
        {
            tmp_cwnd[i] += weight[i] * surplus / wsum;  // more BW for others
            cnt ++;
        }
    }

    if(depth > 2*N) return;
    if(cnt > 1) iter_realloc(sm_rwnd, tmp_cwnd, under_user);
    else return;
}

void BandwidthManager::reuse_switch(vector<double> lr_rwnd, vector<double> lr_cwnd, double capacity)
{
    vector<bool> visited(N, false);
    double wsum = 0.0;
    surplus = 0.0;
    this->capacity = capacity;

/*------------------- Scheme 1 (tested, but not contiuous) -----------*/
    // // 1. method with under-user mark: tested, show its potential on typical 3 mixing flows
    // for (uint32_t i = 0; i < N; i ++)                   // mark and allocate special
    // {
    //     wei_cwnd[i] = weight[i] * capacity;
    //     if (lr_rwnd[i] < lowTh * lr_cwnd[i])            // reuse once
    //     {
    //         tmp_cwnd[i] = (1 + margin) * lowTh * lr_cwnd[i];
    //         surplus += wei_cwnd[i] - tmp_cwnd[i];
    //         visited[i] = true;
    //         wsum += weight[i];
    //     }
    //     else if (lr_rwnd[i] >= highTh * lr_cwnd[i])     // recover once
    //     {
    //         tmp_cwnd[i] = min(wei_cwnd[i], 1.5 * lr_cwnd[i]);
    //         if (tmp_cwnd[i] < wei_cwnd[i])
    //         {
    //             surplus += wei_cwnd[i] - tmp_cwnd[i];
    //             visited[i] = true;
    //             wsum += weight[i];
    //         }
    //     }
    // }
    // for (uint32_t i = 0; i < N; i ++)
    // {
    //     if (!visited[i])
    //         tmp_cwnd[i] = wei_cwnd[i] + surplus * weight[i] / (1 - wsum);
    // }

/*--------------- Scheme 2 (tested) --------------------*/
    // // 1) thought seems correct: if normal flows use weight regardless of their lr_cwnd, it's a little unfair
    // // 2) 0.6-0.9 may be easier for large flow to get more, not good

    // // 1st round: calculate deserved BW
    // for (uint32_t i = 0; i < N; i ++)
    // {
    //     wei_cwnd[i] = weight[i] * capacity;
    //     double x = lr_rwnd[i] / lr_cwnd[i];
    //     if (x < lowTh )
    //         tmp_cwnd[i] = (lowTh + margin) * lr_cwnd[i];                        // y = 70%
    //     else if (x >= lowTh  && x < highTh )
    //         tmp_cwnd[i] =  (x + margin) * lr_cwnd[i];                           // y = 70% + (x - 60%)
    //     else if (x >= highTh  && x < highTh + 0.05)
    //         tmp_cwnd[i] = (10 * (x - highTh) + highTh + margin) * lr_cwnd[i];   // y = 100% + 10(x - 90%)
    //     else tmp_cwnd[i] = 1.5 * lr_cwnd[i];                                    // y = 150%

    //     tmp_cwnd[i] = tmp_cwnd[i] > wei_cwnd[i]? wei_cwnd[i] : tmp_cwnd[i];
    //     surplus += wei_cwnd[i] - tmp_cwnd[i];
    // }

    // // 2nd round: reallocate to each flow
    // for (uint32_t i = 0; i < N; i ++)
    //     tmp_cwnd[i] += surplus * weight[i];

/*---------------- Scheme 3: aggressive recovery ------------*/
// for (uint32_t i = 0; i < N; i ++)
//     {
//         wei_cwnd[i] = weight[i] * capacity;
//         double x = lr_rwnd[i] / lr_cwnd[i];
//         if (x < lowTh )
//             tmp_cwnd[i] = (lowTh + margin) * lr_cwnd[i];                        // y = 70%
//         else if (x >= lowTh  && x < highTh )
//             tmp_cwnd[i] =  (2*(x - lowTh) + margin + lowTh) * lr_cwnd[i];       // y = 70% + 2(x - 60%)
//         else if (x >= highTh)
//             tmp_cwnd[i] = (3 * (x - highTh) + 2 * highTh - lowTh + margin) * lr_cwnd[i];   // y = 130% + 3(x - 90%)

//         tmp_cwnd[i] = tmp_cwnd[i] > wei_cwnd[i]? wei_cwnd[i] : tmp_cwnd[i];
//         surplus += wei_cwnd[i] - tmp_cwnd[i];
//     }
//     for (uint32_t i = 0; i < N; i ++)
//         tmp_cwnd[i] += surplus * weight[i];

/*---------------- Scheme 4: aggressive recovery + strong reuse ------------*/
    vector<double> scale_tmp(N, 0);
    uint32_t idx = 0;           // the flow that has largest rwnd/cwnd
    double max_x = -1;
    for (uint32_t i = 0; i < N; i ++)
    {
        wei_cwnd[i] = weight[i] * capacity;
        lr_cwnd[i] = lr_cwnd[i]? lr_cwnd[i] : 0.001;
        double x = lr_rwnd[i] / lr_cwnd[i];
        scale_tmp[i] = lr_cwnd[i] * capacity / accumulate(lr_cwnd.begin(), lr_cwnd.end(), 0);
        if (x < lowTh )
        {
            // tmp_cwnd[i] = (lowTh + margin) * scale_tmp[i];                        // y = 70%
            tmp_cwnd[i] = (x + margin) * scale_tmp[i];                              // y = x + 10%
            visited[i] = true;
            wsum += weight[i];
        }
        else if (x >= lowTh  && x < highTh )
            tmp_cwnd[i] =  (2*(x - lowTh) + margin + lowTh) * scale_tmp[i];       // y = 70% + 2(x - 60%)
        else if (x >= highTh)
            tmp_cwnd[i] = (3 * (x - highTh) + 2 * highTh - lowTh + margin) * scale_tmp[i];   // y = 130% + 3(x - 90%)

        tmp_cwnd[i] = tmp_cwnd[i] > wei_cwnd[i]? wei_cwnd[i] : tmp_cwnd[i];
        surplus += wei_cwnd[i] - tmp_cwnd[i];

        if (x > max_x)
        {
            max_x = x;
            idx = i;
        }
    }
    bool is_all_under = true;
    for (uint32_t i = 0; i < N; i ++)
        if(!visited[i])
        {
            tmp_cwnd[i] += surplus * weight[i] / (1 - wsum);    // ensure fast reuse
            is_all_under = false;
        }
    if(is_all_under)
        tmp_cwnd[idx] += surplus;             // avoid waste of surplus bandwidth: simple to be good


/*--------------- Choice end. ---------------------------*/
    cur_cwnd = tmp_cwnd;    // ok to discard the max part
}

vector<double> BandwidthManager::getTmpCwnd()
{
    return tmp_cwnd;
}

vector<double> BandwidthManager::getCurCwnd()
{
    return cur_cwnd;
}

vector<double> BandwidthManager::getWeiCwnd()
{
    return wei_cwnd;
}

vector<double> BandwidthManager::getTmpCwndByCapacity(double capacity)
{
    vector<double> res_wnd(N, 0);
    double capa = this->capacity? this->capacity : 1;
    for (uint32_t i = 0; i < N; i ++)
        res_wnd[i] = tmp_cwnd[i] || Simulator::Now().GetSeconds() > 0.5? tmp_cwnd[i] * capacity / capa : weight[i] * capacity;
    return res_wnd;
}

vector<double> BandwidthManager::getCurCwndByCapacity(double capacity)
{
    vector<double> res_wnd(N, 0);
    double capa = this->capacity? this->capacity : 1;
    for (uint32_t i = 0; i < N; i ++)
        res_wnd[i] = cur_cwnd[i] && Simulator::Now().GetSeconds() < 0.5? cur_cwnd[i] * capacity / capa : weight[i] * capacity;
    return res_wnd;
}

void BandwidthManager::logging()
{
    cout << "-- BM: surplus: " << surplus << endl;
    cout << "  Flow     Weighted    Temp      Smoothing " << endl;
    for(uint32_t i = 0; i < N ; i ++)
        cout << "  " << i << "        " << wei_cwnd[i] << "          " << tmp_cwnd[i] << "       " << cur_cwnd[i] << endl;
    cout << endl;
}

/* ------------- Begin: implementation of BeSoftControl, i.e. control state machine  ------------- */

BeSoftControl::BeSoftControl (uint32_t n): nSender(n)
{
    state = vector<BeState> (n, BEON);
    dMax = vector<uint32_t> (n, 0);       // BE on: no drop is allowed
    explStep = 20;
}

void BeSoftControl::update (vector<int> dropReq, vector<uint32_t> rwnd, vector<uint32_t> cwnd, vector<uint32_t> safe_count)
{
    vector<uint32_t> tmpDMax = computeDMax(rwnd, cwnd);     // compute dMax
    for(uint32_t i = 0; i < nSender; i ++)                  // state transition
    {
        uint32_t beNum = rwnd[i] > cwnd[i]? rwnd[i] - cwnd[i] : 0;
        switch (state.at(i))
        {
            case BEON:
                if(dropReq[i] == 3 || dropReq[i] == 4)      // SS drop, rate-based drop or TCP emulation violation
                    state[i] = WARN;
                // else if(dropReq[i] == 2)                    // mwnd (old and new) < rwnd[i] / 4, i.e. BE off condition
                //     state[i] = BEOFF;
                break;
            case WARN:
                if(dropReq[i] == 4)                         // TCP emulation violation
                    state[i] = BEOFF;
                else state[i] = BEON;                       // protected warn's next interval
                break;
            case BEOFF:
                if(dropReq[i] == 0)                         // SS allowed or clean case
                    state[i] = WARN;
                break;
            default:
                break;
        }
        dMax[i] = state[i] == WARN? tmpDMax[i]:
                state[i] == BEON? 0: 
                // beNum;                                                // no exploration, work with mixed case, but under-utilization
                // 2 * rwnd[i];
                // ceil(beNum * (1 - (double)safe_count / 50));       // soft dMax usage for BE off (UDP)
                // ceil(beNum * (1 - pow((double)safe_count / 50, 2.0)));   // cause unstable UDP in mixed case
                explStep? beNum - (uint32_t) (safe_count[i] / explStep) : beNum;
                // max( ceil(beNum * (1 - (double)safe_count / 150)), (double)(beNum - safe_count / 5) );       // work with mixed case
        
        if (state[i] == BEOFF && dMax[i] == beNum - (uint32_t) (safe_count[i] / explStep))
            cout << i << ": Constant explore ";
        else if (state[i] == BEOFF && dMax[i] == ceil(beNum * (1 - (double)safe_count[i] / 150)))
            cout << i << ": Explore by factor ";
        else if (state[i] == BEOFF)
            cout << i << ": no exploration ";
        // else cout << i << ": not BE off" << endl;
        if (state[i] == BEOFF)
            cout << " Factor: " << ceil(beNum * ( 1 - (double)safe_count[i] / 150)) << ", constant: " << beNum - safe_count[i] / explStep << endl;

    }
    last_rwnd.assign(rwnd.begin(), rwnd.end());

}

void BeSoftControl::update (vector<int> dropReq, vector<double> rwnd, vector<double> cwnd, vector<uint32_t> safe_count)
{
    vector<uint32_t> rrwnd, ccwnd;
    for(uint32_t i = 0; i < rwnd.size(); i ++)
    {
        rrwnd.push_back((uint32_t) rwnd[i]);
        ccwnd.push_back((uint32_t) cwnd[i]);
    }
    this->update(dropReq, rrwnd, ccwnd, safe_count);
}

vector<uint32_t> BeSoftControl::computeDMax(vector<uint32_t> rwnd, vector<uint32_t> cwnd)
{
    vector<uint32_t> res(nSender, 2);
    for(uint32_t i = 0; i < nSender; i ++)
    {
        cwnd[i] = cwnd[i] > 0? cwnd[i] : 1; 
        // res[i] = rwnd[i] > cwnd[i]? ceil( log2((double) rwnd[i] / cwnd[i]) ) : 1;
        res[i] = rwnd[i] > cwnd[i]? 2 * ceil( log2((double) rwnd[i] / cwnd[i]) ) : 1;       // times 2 option
    }
    return res;
}

bool BeSoftControl::gradDropCond(vector<uint32_t> mDrop, uint32_t i)
{
    return mDrop.at(i) < dMax.at(i);        // normal control
}

bool BeSoftControl::probDropCond(uint32_t i)
{
    double prob = (double) dMax[i] / (double) last_rwnd[i];
    // cout << "   - flow " << i << " drop cond: dMax = " << dMax[i] << ", last rwnd = " << last_rwnd[i] << ", prob = " << prob << endl;
    return state[i] == BEOFF && (double)rand() / (RAND_MAX + 1.0) < prob;
}

void BeSoftControl::print()
{
    stringstream ss;
    vector<string> stateStr = {"BE on", "Warn", "BE off"};
    ss << "BE Soft Control:   No.  State    dMax    last_rwnd" << endl;
    for (uint32_t i = 0; i < nSender; i ++)
        ss << "                   " << i << "    " << stateStr.at((int)state[i]) << "   " << dMax.at(i) << "  " << last_rwnd[i] << endl;
    cout << ss.str() << endl;    
}

uint32_t BeSoftControl::getNSender()
{
    return nSender;
}

BeState BeSoftControl::getState(uint32_t i)
{
    return state[i];
}

uint32_t BeSoftControl::getTokenCapacity(uint32_t i)
{
    return last_rwnd[i] > dMax[i]? last_rwnd[i] - dMax[i] : 0;
}

/* ------------- Begin: implementation of MyTag, i.e. tag operation ------------- */

TypeId 
MyTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MyTag")
    .SetParent<Tag> ()
    .AddConstructor<MyTag> ()
    .AddAttribute ("SimpleValue",
                   "A simple value",
                   EmptyAttributeValue (),
                   MakeUintegerAccessor (&MyTag::GetSimpleValue),
                   MakeUintegerChecker<uint32_t> ())
  ;
  return tid;
}
TypeId 
MyTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}
uint32_t 
MyTag::GetSerializedSize (void) const
{
  return 4;
}
void 
MyTag::Serialize (TagBuffer i) const
{
  i.WriteU32 (m_simpleValue);
}
void 
MyTag::Deserialize (TagBuffer i)
{
  m_simpleValue = i.ReadU32 ();
}
void 
MyTag::Print (std::ostream &os) const
{
  os << "v=" << (uint32_t)m_simpleValue;
}
void 
MyTag::SetSimpleValue (uint32_t value)
{
  m_simpleValue = value;
}
uint32_t 
MyTag::GetSimpleValue (void) const
{
  return m_simpleValue;
}

/* ------------- Begin: implementation of MyApp, i.e. application setting ------------- */

MyApp::MyApp ()
  : m_socket (0), 
    m_peer (), 
    m_packetSize (0), 
    //m_nPackets (0), 
    m_dataRate (0), 
    m_sendEvent (), 
    m_running (false), 
    //m_packetsSent (0)
    m_tagValue (0),
    m_cnt (0)
{
    isTrackPkt = false;
    m_rid = rand() % 10000;
}

MyApp::~MyApp()
{
  m_socket = 0;
}

void
//MyApp::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate)
MyApp::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, DataRate dataRate)
{
  m_socket = socket;
  m_peer = address;
  m_packetSize = packetSize;
  //m_nPackets = nPackets;
  m_dataRate = dataRate;
}

void
MyApp::SetDataRate(DataRate rate)
{
  m_dataRate = rate;
}

void
MyApp::SetTagValue(uint32_t value)
{
  m_tagValue = value;
}

void
MyApp::StartApplication (void)
{
  m_running = true;
  //m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  SendPacket ();
  NS_LOG_INFO("Start application!");
}

void 
MyApp::StopApplication (void)
{
  m_running = false;

  if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }

  if (m_socket)
    {
      m_socket->Close ();
    }
}

void 
MyApp::SendPacket (void)
{
    NS_LOG_FUNCTION("  Begin.  ");
    //create the tags
    MyTag tag;
    if(!isTrackPkt)  tag.SetSimpleValue (m_tagValue);
    else   tag.SetSimpleValue (m_tagValue * tagScale + ++m_cnt);
    // else tag.SetSimpleValue (m_tagValue * tagScale + m_rid);
    m_rid = rand() % 10000;

    stringstream ss;
    ss << "- Tx: " << m_tagValue - 1 << ". " << m_cnt << ", rate: " << static_cast<double> (m_dataRate.GetBitRate()) / 1e6 << " Mbps";
    //   ss << "- Tx: " << m_tagValue - 1 << ". " << m_rid << ", rate: " << static_cast<double> (m_dataRate.GetBitRate()) / 1e6 << " Mbps";

    NS_LOG_FUNCTION (ss.str());        // TX tested

    Ptr<Packet> packet = Create<Packet> (m_packetSize);
    packet -> AddPacketTag (tag);     //add tags
    m_socket->Send (packet);

    ScheduleTx ();
}

void
MyApp::StartAck (void)
{
  m_running = true;
  //m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
}


void
MyApp::SendAck (uint32_t ackNo)
{
    MyTag tag;
    tag.SetSimpleValue (m_tagValue * tagScale + ackNo);        // need testing, subject to change

    Ptr<Packet> packet = Create<Packet> (15);
    packet->AddPacketTag (tag);             // add tag to packet
    m_socket->Send (packet);

}

void 
MyApp::ScheduleTx (void)
{
  NS_LOG_FUNCTION("  Begin.  ");
  if (m_running)
    {
      Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate ())));
      m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this);
    }
   NS_LOG_FUNCTION("  Over.  ");
}

Ptr<Socket> MyApp::GetSocket()
{
    return m_socket;
}

/* ------------- Begin: implementation of MiddlePoliceBox, i.e. main part ------------- */

MiddlePoliceBox::MiddlePoliceBox(vector<uint32_t> num, double tStop, ProtocolType prot, FairType fairn, double pktSize, bool trackPkt, double beta, 
                                vector<double> th, uint32_t iMID, uint32_t wnd, vector<bool> fls, double alpha, double sc, double explStep, bool isEDrop)
{
    // constant setting
    uint32_t nMonitor = num[1];
    nSender = nMonitor;       // N_ctrl
    nReceiver = nMonitor;
    nClient = num[2];
    nAttacker = num[3];

    nCross = num[0] - num[1];

    slrWnd = wnd;
    isEarlyDrop = isEDrop;
    isEbrc = fls[0];
    isTax = fls[1]; 
    bypassMacRx = fls[2];
    is_monitor = fls[3];
    isCA = vector<bool>(nSender, false);
    this->tStop = tStop;
    this->beta = beta;
    // lrTh = th; 
    slrTh = th.at(0);
    llrTh = th.at(1);
    this->scale = sc;

    // internal parameters
    // srand(time(0));          // should be global in main
    if(iMID == 0) this->MID = rand() % 10000;
    else this->MID = iMID;
    protocol = prot;
    fairness = fairn;
    normSize = 8.0 / 1000;      // byte -> kbit
    this->pktSize = pktSize;
    this->alpha = alpha;
    pl0 = vector<double> (nSender, 0);
    isStop = false;
    isStatReady = true;
    isTrackPkt = trackPkt;
    weight = vector<double> (nSender, 1.0/nSender);     // Persender by default
    Bm = BandwidthManager(weight);
    u_cnt = vector<uint32_t> (nSender, 1);
    tax = vector<uint32_t> (nSender, 0);
    lastTax = vector<uint32_t> (nSender, 0);
    sswnd = vector<double> (nSender, 10);             // actually should respective to detect wnd size
    
    // monitor setting
    rwnd = vector<uint32_t> (nSender, 0);   // rwnd: use to record each senders' rate
    cwnd = vector<uint32_t> (nSender, 0);   // cwnd: use to control senders' rate
    lDrop = vector<uint32_t> (nSender, 0);
    qDrop = vector<uint32_t> (nSender, 0);
    nAck = vector<uint32_t> (nSender, 0);
    last_lDrop = vector<uint32_t> (nSender, 0);
    mDrop = vector<uint32_t> (nSender, 0);
    last_rwnd = vector<uint32_t> (nSender, 0);

    sm_capacity = 0;
    sm_rwnd = vector<double> (nSender, 0);
    sm_cwnd = vector<double> (nSender, 0);

    totalRx = vector<uint32_t> (nSender, 0);
    totalRxByte = vector<uint32_t> (nSender, 0);
    totalTxByte = vector<uint32_t> (nSender, 0);
    totalDrop = vector<uint32_t> (nSender, 0);
    totalMDrop = vector<uint32_t> (nSender, 0);
    txRate = vector<double> (nSender, 0);
    dRate = vector<double> (nSender, 0);
    lastTx = vector<uint32_t> (nSender, 0);
    lastRx2 = vector<uint32_t>(nSender, 0);
    ltDrop = vector<uint32_t> (nSender, 0);
    ltTx = vector<uint32_t> (nSender, 0);
    mwnd = vector<double> (nSender, 0);
    bePkt = vector<uint32_t> (nSender, 0);
    lastArrival = vector<double>(nSender, 0.0);
    lastDrop = 0;
    lastRx = 0;
    slr = 0.0;
    llr = vector<double> (nSender, 0.0);

    Acka = AckAnalysis(nSender);            // need test
    Bsc = BeSoftControl(nSender);
    Bsc.explStep = explStep;
    Ssm = SlowstartManager(weight, safe_Th); 
    ssDrop = vector<bool> (nSender, false);
    // Lrm = LongRunSmoothManager (nSender, 0.02, 25);      // 0.02, 25: tested in moving avg scheme
    Lrm = LongRunSmoothManager (nSender, 0.07, 8);      // 0.07, 8: works normally with exp mov avg


    congWnd = vector<uint32_t> (nSender, 0);
    rtt = vector<double> (nSender, 0);

    this->explStep = explStep;

    // redundant, mainly for debug
    txwnd = vector<uint32_t> (nSender, 0);
    txDwnd = vector<uint32_t> (nSender, 0);
    phyTxDwnd = vector<uint32_t> (nSender, 0);
    phyRxDwnd = vector<uint32_t> (nSender, 0);    
    rxwnd = vector<uint32_t> (nSender, 0);
    tcpwnd = vector<uint32_t> (nSender, 0);
    ackwnd = vector<int> (nSender, 0);
    addr2index = map<uint32_t, uint32_t> ();
    ip2prot = map<Ipv4Address, ProtocolType> ();
    dropWnd = vector<vector<uint32_t>> (3, vector<uint32_t>(nSender, 0));       // for macTxDrop2, phyTxDrop2, phyRxDrop2
    rho = 1;
    index2des = map<uint32_t, Ipv4Address> ();

    // for cross traffic
    totalCrossByte = vector<uint32_t> (nCross, 0);         // use the number of all flows
    lastCross = vector<uint32_t> (nCross, 0);

    // output setting
    string folder = "MboxStatistics";
    vector<string> name = {"DataRate", "LLR", "Dwnd", "Rwnd", "TcpWnd",         // open to extension
                        "CongWnd", "Rtt", "sm_rwnd", "sm_cwnd", "mDrop",
                        "lDrop", "scMdrop", "scLdrop", "sswnd", "tmp_cwnd",
                        "cur_cwnd", "wei_cwnd", "RttLlr", "AckLatency"};
    vector<string> sname = {"SLR", "QueueSize", "TotalTcp", "QueueDrop", "DevQueueSize", "Weight", "RedQueueSize", "LrCapacity"};       // for data irrelated to sender 
    for(uint32_t i = 0; i < name.size(); i ++)
        name[i] += "_" + to_string(MID);
    for(uint32_t i = 0; i < sname.size(); i ++)
        sname[i] += "_" + to_string(MID);

    for(uint32_t i = 0; i < nSender; i ++)
    {
        fnames.push_back(vector<string>());
        fout.push_back(vector<ofstream>());
        for(uint32_t j = 0; j < name.size(); j ++)
        {
            fnames.at(i).push_back(folder + "/" + name[j] + "_" + to_string(i) + ".dat");
            remove(fnames.at(i).at(j).c_str());
            fout.at(i).push_back(ofstream(fnames.at(i).at(j), ios::app | ios::out));
        }
    }
    for(uint32_t i = 0; i < nCross; i ++)
    {
        fnames.push_back(vector<string>());
        fout.push_back(vector<ofstream>());
        string cname = folder + "/" + name[0] + "_" + to_string(i + nSender) + ".dat";
        fnames.at(i + nSender).push_back(cname);
        remove(cname.c_str());
        fout[nSender + i].push_back(ofstream(cname, ios::app | ios::out));
    }

    for(uint32_t i = 0; i < sname.size(); i ++)
    {
        singleNames.push_back(folder + "/" + sname[i] +"_0.dat");
        remove(singleNames.at(i).c_str());
        singleFout.push_back(ofstream(singleNames.at(i), ios::app | ios::out));
    }

    // log function only for debug
    stringstream ss;
    ss << "\n # sender: " << nSender << ", # receiver: " << nReceiver << endl;
    ss << " # client: " << nClient << ", # attacker: " << nAttacker << endl;
    ss << " Stop time: " << tStop << endl;
    ss << " Test file name llr[0]: " << fnames.at(0).at(1) << endl;
    ss << " Test file name slr: " << singleNames.at(0) << endl;
    NS_LOG_INFO(ss.str());
}

MiddlePoliceBox::MiddlePoliceBox(const MiddlePoliceBox& mb):
  rwnd(mb.rwnd), last_rwnd(mb.last_rwnd), cwnd(mb.cwnd), mDrop(mb.mDrop), sm_capacity(mb.sm_capacity), sm_rwnd(mb.sm_rwnd), sm_cwnd(mb.sm_cwnd),lDrop(mb.lDrop), last_lDrop(mb.last_lDrop), totalRx(mb.totalRx), totalRxByte(mb.totalRxByte), totalTxByte(mb.totalTxByte),
  totalDrop(mb.totalDrop), qDrop(mb.qDrop), nAck(mb.nAck), totalMDrop(mb.totalMDrop), lastDrop(mb.lastDrop), lastRx(mb.lastRx), lastTx(mb.lastTx), lastRx2(mb.lastRx2), lastArrival(mb.lastArrival), slr(mb.slr), llr(mb.llr), dRate(mb.dRate), 
  RTT(mb.RTT), tRto(mb.tRto), txRate(mb.txRate), nSender(mb.nSender), nClient(mb.nClient), nAttacker(mb.nAttacker), nReceiver(mb.nReceiver), nCross(mb.nCross), slrWnd(mb.slrWnd),
  isEarlyDrop(mb.isEarlyDrop), isEbrc(mb.isEbrc), isTax(mb.isTax), bypassMacRx(mb.bypassMacRx), is_monitor(mb.is_monitor), isCA(mb.isCA), tStop(mb.tStop), beta(mb.beta), lrTh(mb.lrTh), slrTh(mb.slrTh), llrTh(mb.llrTh), MID(mb.MID), fEID(mb.fEID), 
  sEID(mb.sEID), cEID(mb.cEID), device(mb.device), fnames(mb.fnames), singleNames(mb.singleNames), normSize(mb.normSize), pktSize(mb.pktSize), alpha(mb.alpha), pl0(mb.pl0), protocol(mb.protocol), fairness(mb.fairness), isStop(mb.isStop), 
  isStatReady(mb.isStatReady), isTrackPkt(mb.isTrackPkt), weight(mb.weight), Bm(mb.Bm), u_cnt(mb.u_cnt), tax(mb.tax), lastTax(mb.lastTax), sswnd(mb.sswnd), txwnd(mb.txwnd), txDwnd(mb.txDwnd), phyTxDwnd(mb.phyTxDwnd), 
  phyRxDwnd(mb.phyRxDwnd), rxwnd(mb.rxwnd), dropWnd(mb.dropWnd), tcpwnd(mb.tcpwnd), ackwnd(mb.ackwnd), addr2index(mb.addr2index), ip2prot(mb.ip2prot), congWnd(mb.congWnd), rtt(mb.rtt), Acka(mb.Acka), Bsc(mb.Bsc), Ssm(mb.Ssm), ssDrop(mb.ssDrop), Lrm(mb.Lrm), ltDrop(mb.ltDrop), ltTx(mb.ltTx),
  mwnd(mb.mwnd), scale(mb.scale), bePkt(mb.bePkt), safe_count(mb.safe_count), safe_Th(mb.safe_Th), explStep(mb.explStep), rho(mb.rho), rxAckNo(mb.rxAckNo), index2des(mb.index2des), counter(mb.counter), lr_period(mb.lr_period), totalCrossByte(mb.totalCrossByte), lastCross(mb.lastCross)
{
    NS_LOG_FUNCTION(" Copy constructor. ");
    for(uint32_t i = 0; i < nSender; i ++)
    {
        fout.push_back(vector<ofstream>());
        for(uint32_t j = 0; j < fnames.at(i).size(); j ++)
        {
            remove(fnames.at(i).at(j).c_str());
            fout.at(i).push_back(ofstream(fnames.at(i).at(j), ios::app | ios::out));
        }
    }
    for(uint32_t i = 0; i < nCross; i ++)
    {
        fout.push_back(vector<ofstream>());
        remove(fnames[i + nSender][0].c_str());
        fout[i + nSender].push_back(ofstream(fnames[i + nSender][0], ios::app | ios::out));
    }
    for(uint32_t i = 0; i < singleNames.size(); i ++)
    {
        remove(singleNames.at(i).c_str());
        singleFout.push_back(ofstream(singleNames.at(i), ios::app | ios::out));
    }
}

MiddlePoliceBox& MiddlePoliceBox::operator= (const MiddlePoliceBox & mb)
{
    NS_LOG_FUNCTION(" Move assignment. ");
    rwnd = mb.rwnd;
    last_rwnd = mb.last_rwnd;
    cwnd = mb.rwnd;
    mDrop = mb.mDrop;
    lDrop = mb.lDrop;
    qDrop = mb.qDrop;
    nAck = mb.nAck;

    last_lDrop = mb.last_lDrop;

    sm_capacity = mb.sm_capacity;
    sm_rwnd = mb.sm_rwnd;
    sm_cwnd = mb.sm_cwnd;

    totalRx = mb.totalRx;
    totalRxByte = mb.totalRxByte;
    totalTxByte = mb.totalTxByte;
    totalDrop = mb.totalDrop;
    totalMDrop = mb.totalMDrop;
    lastDrop = mb.lastDrop;
    lastRx = mb.lastRx;
    lastTx = mb.lastTx;
    lastRx2 = mb.lastRx2;
    lastArrival = mb.lastArrival;
    RTT = mb.RTT;
    tRto = mb.tRto;
    slr = mb.slr;
    llr = mb.llr;
    dRate = mb.dRate;
    txRate = mb.txRate;
    nSender = mb.nSender;
    nClient = mb.nClient;
    nAttacker = mb.nAttacker;
    nReceiver = mb.nReceiver;
    slrWnd = mb.slrWnd;
    isEarlyDrop = mb.isEarlyDrop;
    isEbrc = mb.isEbrc;
    isTax = mb.isTax;
    bypassMacRx = mb.bypassMacRx;
    is_monitor = mb.is_monitor;
    isCA = mb.isCA;
    tStop = mb.tStop;
    beta = mb.beta;
    lrTh = mb.lrTh;
    slrTh = mb.slrTh;
    llrTh = mb.llrTh;
    MID = mb.MID;
    fEID = mb.fEID;
    sEID = mb.sEID;
    cEID = mb.cEID;
    device = mb.device;
    fnames = mb.fnames;
    singleNames = mb.singleNames;
    normSize = mb.normSize; 
    pktSize = mb.pktSize;
    alpha = mb.alpha;
    pl0 = mb.pl0;
    protocol = mb.protocol;
    fairness = mb.fairness;
    isStop = mb.isStop;
    isStatReady = mb.isStatReady;
    isTrackPkt = mb.isTrackPkt;
    weight = mb.weight;
    Bm = mb.Bm;
    u_cnt = mb.u_cnt;
    tax = mb.tax;
    lastTax = mb.lastTax;
    sswnd = mb.sswnd;
    txwnd = mb.txwnd;
    txDwnd = mb.txDwnd;
    phyTxDwnd = mb.phyTxDwnd;
    phyRxDwnd = mb.phyRxDwnd;
    rxwnd = mb.rxwnd;
    dropWnd = mb.dropWnd;
    tcpwnd = mb.tcpwnd;
    ackwnd = mb.ackwnd;
    addr2index = mb.addr2index;
    ip2prot = mb.ip2prot;

    ltDrop = mb.ltDrop;
    ltTx = mb.ltTx;
    mwnd = mb.mwnd;
    scale = mb.scale;
    bePkt = mb.bePkt;

    congWnd = mb.congWnd;
    rtt = mb.rtt;
    Acka = mb.Acka;
    Bsc = mb.Bsc;
    ssDrop = mb.ssDrop;
    Ssm = mb.Ssm;
    Lrm = mb.Lrm;
    safe_count = mb.safe_count;
    safe_Th = mb.safe_Th;
    explStep = mb.explStep;
    counter = mb.counter;
    lr_period = mb.lr_period;

    rho = mb.rho;
    index2des = mb.index2des;
    rxAckNo = mb.rxAckNo;

    // for cross traffic monitoring
    nCross = mb.nCross;
    totalCrossByte = mb.totalCrossByte;
    lastCross = mb.lastCross;

    for(uint32_t i = 0; i < nSender; i ++)
    {
        fout.push_back(vector<ofstream>());
        for(uint32_t j = 0; j < fnames.at(i).size(); j ++)
        {
            remove(fnames.at(i).at(j).c_str());
            fout.at(i).push_back(ofstream(fnames.at(i).at(j), ios::app | ios::out));
        }
    }
    for(uint32_t i = 0; i < nCross; i ++)
    {
        fout.push_back(vector<ofstream>());
        remove(fnames[i + nSender][0].c_str());
        fout[i + nSender].push_back(ofstream(fnames[i + nSender][0], ios::app | ios::out));
    }
    for(uint32_t i = 0; i < singleNames.size(); i ++)
    {
        remove(singleNames.at(i).c_str());
        singleFout.push_back(ofstream(singleNames.at(i), ios::app | ios::out));
    }
    return *this;
}

MiddlePoliceBox::~MiddlePoliceBox()
{
    NS_LOG_FUNCTION(" Destructor. ");
    if(!singleFout.empty())
        for(uint32_t i = 0; i < singleFout.size(); i ++)
            singleFout.at(i).close();
    if(!fnames.empty())
        for(uint32_t i = 0; i < nSender; i ++)
        {
            for(uint32_t j = 0; j < fnames.at(i).size(); j ++)
                fout.at(i).at(j).close();
        }
}

void
MiddlePoliceBox::install(Ptr<NetDevice> device)
{
    NS_LOG_FUNCTION("  Install tx device. ");
    Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice> (device);
    this->device = p2pDev;
}

void
MiddlePoliceBox::install(NetDeviceContainer nc)
{
    NS_LOG_FUNCTION("  Install rx devices. ");
    macRxDev = vector<Ptr<PointToPointNetDevice> >(nc.GetN());
    for(uint32_t i = 0; i < nc.GetN(); i ++)
        macRxDev.at(i) = DynamicCast<PointToPointNetDevice> (nc.Get(i));
}

// debug only
void
MiddlePoliceBox::TcPktInQ(uint32_t vOld, uint32_t vNew)
{
    // NS_LOG_INFO("Tc pkt in q! size: " + to_string(vNew));
    singleFout.at(1) << Simulator::Now().GetSeconds() << " " << vNew << endl;
}

void
MiddlePoliceBox::TcPktInRed(uint32_t vOld, uint32_t vNew)
{
    // NS_LOG_INFO("Tc pkt in q! size: " + to_string(vNew));
    singleFout.at(6) << Simulator::Now().GetSeconds() << " " << vNew << endl;
}

void
MiddlePoliceBox::DevPktInQ(uint32_t vOld, uint32_t vNew)
{
    // NS_LOG_INFO("Tc pkt in q! size: " + to_string(vNew));
    singleFout.at(4) << Simulator::Now().GetSeconds() << " " << vNew << endl;
}

vector<uint32_t> 
MiddlePoliceBox::assignRandomLoss(vector<uint32_t> tax, vector<double> Ebed, uint32_t N)        // unit test passed
{
    NS_LOG_FUNCTION("  Assign random loss.");
    if(N) NS_LOG_INFO("  - Assign random " + to_string(N) + " loss... ");
    double totalEbed = accumulate(Ebed.begin(), Ebed.end(), 0.0);
    if(totalEbed == 0) 
    {
        return tax;          // exception: if EBED all 0, that means no excess BE pkt to drop (even if PLR > 0)
    }
    for(uint32_t j = 0; j < N; j ++)
    {
        double samp = (double) rand() / RAND_MAX;
        vector<double> rg = {0.0, Ebed[0] / totalEbed};
        for(uint32_t i = 0; i < tax.size(); i ++)
        {
            if(samp >= rg[0] && samp < rg[1])
            {
                tax[i] += 1;
                break;
            }
            else
            {
                rg[0] = rg[1];
                cout << " " << rg[0];
                NS_ASSERT_MSG(i + 1 < nSender, "Sample " + to_string(samp) + " not in [0,1]??");
                rg[1] += Ebed[i + 1] / totalEbed;       // theoretically it shouldn't overflow
            }
            cout << endl;
        }
    }
    return tax;
}

void 
MiddlePoliceBox::controlDrop(uint32_t index, uint32_t seqNo)
{
    // --------------------- Commented for ns-3.27 to avoid patch point-to-point device ------------------
    // static uint32_t intCnt = 0;
    // // if(index >= nSender) return;         // should not happen
    // if(bypassMacRx) device->SetEarlyDrop(true);
    // else if (!is_monitor)
    // {
    //     if(!macRxDev.at(index)->GetEarlyDrop())     // only drop once
    //     {
    //         macRxDev.at(index)->SetEarlyDrop(true);
    //         // if(intCnt % 5 == 1) 
    //         NS_LOG_FUNCTION("  Early drop set!");
    //         intCnt ++;
    //     }
    //     mDrop[index] ++;
    //     totalMDrop[index] ++;
    //     isCA[index] = true;
    //     Acka.push_back(index, seqNo);

    //     if (index == 0 && seqNo == 1)
    //         cout << "Control Drop here" << endl;
    // }
    NS_LOG_FUNCTION("Control without drop here");
}

int
MiddlePoliceBox::assignLoss(uint32_t index)
{
    int dropIndex = -1;
    double value = 10000;  
    for(uint32_t i = 0; i < nSender; i ++)
    {   
        if(rwnd[i] <= cwnd[i]) continue;        // only consider best-effort flow to drop
        if(i == index) continue;
        double v = (double) (lDrop[i] + mDrop[i] + 1) * weight[i] / (rwnd[i] - cwnd[i]);        // 1) consider all pkt sent; 2) times weight
        cout << "  i: " << i << "; value: " << v << endl;
        if( v < value ) 
        {
            dropIndex = i;
            value = v;
        }
    }
    stringstream ss;
    ss << " ->  Loss assigned to flow " << dropIndex << "; value: " << value;
    NS_LOG_FUNCTION(ss.str());
    return dropIndex;
}

void
MiddlePoliceBox::onMacRx(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    vector<int> ic = ExtractIndexFromTag(p);
    int index = ic.at(0);
    int cnt = ic.at(1);
    NS_LOG_DEBUG("MacRx: " << index << ". " << cnt << ", txwnd = " << txwnd[index]);
    if(index < 0 || index >= nSender) return;

    ProtocolType protocol = ip2prot[getIpSrcAddr(p)];
    // if (rwnd[index] < 10)
    if(protocol == TCP)
        NS_LOG_FUNCTION ("  - " << index << ". " << cnt << ": flow protocol: " << (protocol == TCP? "TCP" : "UDP"));

    
    stringstream ss2;
    if(bypassMacRx)
    {
        txwnd[index] ++;
        Acka.insert(p->Copy(), index);
    }

    // Acka.insert(p->Copy(), index);   // no need, onMacTx already added
    Acka.insert_pkt(index, cnt);
    if (rwnd[index] == 0)
        index2des[index] = getIpDesAddr(p);

    if(isTrackPkt)
    {
        ss2 << endl;
        p->Copy()->Print(ss2);
        // NS_LOG_INFO(ss2.str());
    }

    // compute and update
    rwnd[index] ++;
    double bytes = getPktSizes(p->Copy(), protocol).at(3);     // tcp bytes
    uint32_t seqNo = protocol == TCP? getTcpSequenceNo(p):cnt;
    totalTxByte[index] += bytes;
    // NS_LOG_INFO(" MacRx Begin: " + to_string(index) + ", seq = " + to_string(seqNo));
    // NS_LOG_INFO (this << " MacRx: " << index << ". " << seqNo << ": " << bytes << " B");

    // update sswnd
    if(!isCA[index]) sswnd[index] += rho;            // ip pkt size / tcp pkt size (in avg)

    // manual flag
    bool isSs = false;

    // best-effort packet handling: traditional style & EBRC method
    bool BeCond = rwnd[index] > cwnd[index];
    bool PriCond = last_rwnd[index] - mDrop[index] - lDrop[index] <= cwnd[index];   // at the end of 1 interval
    if(!PriCond) bePkt[index] ++;

    // bool BeCond = true;
    bool dropCond = isEarlyDrop && BeCond && (sm_rwnd[index] > b * sm_cwnd[index]
                            || rwnd[index] > sswnd[index]);

    stringstream ss3;
    bool ProbCond = Bsc.probDropCond(index);
    ss3 << " Flow " << index << ": PriCond: " << PriCond << "; ProbCond: " << ProbCond << "; seq: " << seqNo << endl;



    // if(isEarlyDrop && BeCond && rwnd[index] > sswnd[index]) // classic drop for BE off case (BE in the end)
    // {
    //     if(Bsc.gradDropCond(mDrop, index))
    //     {
    //         NS_LOG_INFO(" Drop classically in BE off: " + to_string(index) + ". " + to_string(seqNo));
    //         controlDrop(index, seqNo);
    //     }
    // }
    // else if(dropCond)        // final version: tx rate > EBRC rate, and Best-Effort pkt

    // part of uniform sampling, still active 
    // if(isEarlyDrop && Bsc.getState(index) == BEOFF && !PriCond && ProbCond)    // probable drop for BE off case
    // {
    //     ss3 << " Packet " << index << ". " << seqNo << ": Prabability drop in BE off!" << endl;
    //     controlDrop(index, seqNo);
    // }

    if (index == 0 && seqNo == 1)
        cout << "   - State: " << Bsc.getState(index) << ", BeCond: " << (bool)BeCond << ", sm_rwnd: " << sm_rwnd[index] << ", sm_cwnd: " << sm_cwnd[index]
            << ", ssDrop: " << (bool)ssDrop[index] << endl;

    if(isEarlyDrop && Bsc.getState(index) == BEOFF && BeCond)       // naive BE off drop
    {
        if(Bsc.gradDropCond(mDrop, index))
            controlDrop(index, seqNo);
    }
    else if(isEarlyDrop && Bsc.getState(index) == WARN && BeCond && sm_rwnd[index] > b * sm_cwnd[index])       // normal drop for rate-based control
    {
        ss3 << "  Rate based condition is true: " << index << ". " << seqNo << ": " << rwnd[index] << " > " << cwnd[index] << endl;
        if(Bsc.gradDropCond(mDrop, index))
        {
            controlDrop(index, seqNo);        // tcp friendliness judge
            if(!isEbrc && tax[index] > 0) tax[index] --;

            // if(sm_rwnd[index] > b * sm_cwnd[index]) NS_LOG_INFO("   Smoothed rate based drop on " + to_string(index));
            // else if(isTax && tax[index] > 0) NS_LOG_INFO("   Impose tax on " + to_string(index));
            // else NS_LOG_INFO("  Drop by emulating TCP cwnd!");              
        }
    }
    else if(isEarlyDrop && ssDrop[index])       // drop for slow start with rate-based control scheme
    {
        if(Bsc.gradDropCond(mDrop, index))
        {
            ss3 << "  SS drop here!" << index << ". " << seqNo << endl;
            controlDrop(index, seqNo);
            ssDrop[index] = false;
        }
    }
    else if(isEbrc && isTax && isEarlyDrop && tax[index] > 0 && rwnd[index] > cwnd[index])       // final version: after priority loss occurred, imposing tax
    {
        NS_LOG_INFO("  Impose tax on " + to_string(index));
        if(Bsc.gradDropCond(mDrop, index))
        {
            controlDrop(index, seqNo);
            tax[index] --;
        } 
    }
    else 
    {   
        // slr update: should be proper just after updating rx
        totalRx[index] ++;      // for slr update 
        uint32_t curRx = accumulate(totalRx.begin(), totalRx.end(), 0);
        if(curRx - lastRx == slrWnd)
        {
            uint32_t curDrop = accumulate(totalDrop.begin(), totalDrop.end(), 0);
            slr = (double) (curDrop - lastDrop) / slrWnd;
            lastRx = curRx;
            lastDrop = curDrop; 
        }
    }
    // NS_LOG_INFO(ss3.str());          // all control drop logging

    // update loss rate for EBRC computation
    stringstream ss1;
    // double beta = 1 / 25.0;   // should also be chosen carefully (1 / 40 for 5s, 50Mbps per flow case)
    double beta = 1 / 20.0;      // 120M EBRC bug: debug option
    // double beta = 1 / 50.0;     // for bandwidth = 200Mbps
    double plWnd = 500;       // should correspond to the data rate
    if(ltTx[index] >= (uint32_t)plWnd)
    {
        double newPl = (double)(totalDrop[index] + totalMDrop[index] - ltDrop[index]) / plWnd;
        pl0[index] = (1 - beta) * pl0[index] + beta * newPl;
        ltTx[index] = 0;
        ltDrop[index] = totalDrop[index] + totalMDrop[index];
        // ss1 << "    -> pl0 = " << pl0[index] << "; new pl = " << newPl << ".";
    }
    ltTx[index] ++;
    if(!ss1.str().empty()) NS_LOG_INFO(ss1.str());

    // record positive SLR
    if(slr > 0.0)       // also record here, otherwise slr is probably all 0 
    {
        // NS_LOG_INFO("SLR: " + to_string(slr));
        singleFout.at(0) << Simulator::Now().GetSeconds() << " " << slr << endl;      // record slr
    }
}

void
MiddlePoliceBox::onMacRxWoDrop(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    vector<int> ic = ExtractIndexFromTag(p);
    int index = ic.at(0);
    int cnt = ic.at(1);
    if(index < 0 || index >= nSender) return;
    ProtocolType protocol = ip2prot[getIpSrcAddr(p)];

    if(bypassMacRx)
    {
        txwnd[index] ++;
        NS_LOG_FUNCTION("Before insert: index = " + to_string(index));
        Acka.insert(p->Copy(), index);

    }

    // compute and update
    rwnd[index] ++;
    double bytes = getPktSizes(p->Copy(), protocol).at(3);     // tcp bytes
    uint32_t seqNo = protocol == TCP? getTcpSequenceNo(p):-1;
    totalTxByte[index] += bytes;
    NS_LOG_FUNCTION(" MacRx Begin: " + to_string(index) + ", cnt = " + to_string(cnt));

    // update sswnd
    if(!isCA[index])
        sswnd[index] += rho;            // ip pkt size / tcp pkt size

    // manual flag
    bool isSs = false;

    // slr update: should be proper just after updating rx
    totalRx[index] ++;      // for slr update 
    uint32_t curRx = accumulate(totalRx.begin(), totalRx.end(), 0);
    if(curRx - lastRx == slrWnd)
    {
        uint32_t curDrop = accumulate(totalDrop.begin(), totalDrop.end(), 0);
        slr = (double) (curDrop - lastDrop) / slrWnd;
        lastRx = curRx;
        lastDrop = curDrop; 
    }

    // update loss rate for EBRC computation
    stringstream ss1;
    double beta = 1 / 25.0;   // should also be chosen carefully (1 / 40 for 5s, 50Mbps per flow case)
    double plWnd = 500;       // should correspond to the data rate
    if(ltTx[index] >= (uint32_t)plWnd)
    {
        double newPl = (double)(totalDrop[index] + totalMDrop[index] - ltDrop[index]) / plWnd;
        pl0[index] = (1 - beta) * pl0[index] + beta * newPl;
        ltTx[index] = 0;
        ltDrop[index] = totalDrop[index] + totalMDrop[index];
    }
    ltTx[index] ++;

}

void
MiddlePoliceBox::onRedDrop(Ptr<const QueueDiscItem> qi)
{
    NS_LOG_FUNCTION("   Begin.  ");
    vector<int> ic = ExtractIndexFromTag(qi->GetPacket());
    int index = ic.at(0);
    int cnt = ic.at(1);
    ProtocolType protocol;
    if (index < 0 || index >= nSender) return;
    NS_LOG_INFO("Flow " << index << ": Red queue drop here.");
}

void
MiddlePoliceBox::onQueueDrop(Ptr<const QueueDiscItem> qi)
{
    NS_LOG_FUNCTION("  Begin.  ");
    stringstream sss;
    qi->GetPacket()->Print(sss);
    if(sss.str().substr(0, 14) == "ns3::TcpHeader")
    {
        cout << "getIpSrcAddr:: No ip header!" << endl;
        return;
    }

    vector<int> ic = ExtractIndexFromTag(qi->GetPacket());
    int index = ic.at(0);
    int cnt = ic.at(1);
    if (index < 0) return;

    ProtocolType protocol;
    uint32_t seqNo;

    NS_LOG_INFO("Location 0.5");
    // cout << " - getIpSrcAddr: p size = " << qi->GetPacket()->GetSize() << endl;      // for debug



    if (qi->GetPacket()->GetSize() <= 1410)
        protocol = UDP;
    else protocol = ip2prot[getIpSrcAddr(qi->GetPacket())];

NS_LOG_INFO("Location 1.0");
    
    seqNo = protocol == TCP? getTcpSequenceNoInQueue(qi->GetPacket()):cnt;
    if(index < 0 || index >= nSender) return;
    stringstream ss;
    Ptr<Packet> p = qi->GetPacket();
    // p->Print(ss);

    // rwnd[index] ++;          // compensate for the dropped packet not count in MacTx, should be delete if later mbox is before tc layer
    // lDrop[index] ++;
    // totalDrop[index] ++;    // for slr
    qDrop[index] ++;
    Acka.push_back(index, seqNo);
    isCA[index] = true;
    NS_LOG_FUNCTION(" link drop of queue [" + to_string(index) + "] = " + to_string(lDrop[index]));
    singleFout.at(3) << Simulator::Now().GetSeconds() << " " << 4 << endl;

    // single debug
    if (index == 0 && cnt == 0)
        cout << "Queue Drop here" << endl;

    // for debug only
    if(isTrackPkt)
    {
        ss << " -- onTbfQueueDrop: " << index << ". " << cnt
        << ": seq = " << (protocol == TCP? getTcpSequenceNoInQueue(p):cnt) << ", " << getPktSizesInQueue(p, protocol).at(3) << " B, qDrop = " << qDrop[index];        
        NS_LOG_INFO(ss.str());  
    }

}

void
MiddlePoliceBox::onPktRx(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    vector<int> ic = ExtractIndexFromTag(p);
    int index = ic.at(0);
    int cnt = ic.at(1);
    if(index < 0) return;
    ProtocolType protocol = ip2prot[getIpSrcAddr(p)];
    double bytes = getPktSizes(p->Copy(), protocol).at(3);     // tcp bytes
    if(index < nSender)
    {
        totalRxByte[index] += bytes;
        rxwnd[index] ++;
    }
    else totalCrossByte[index - nSender] += bytes;
}

// following: redundant sink for debug and packet analysis

void
MiddlePoliceBox::onMacTx(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    stringstream ss2;
    vector<int> ic = ExtractIndexFromTag(p);
    // p->Print(ss2);       

    int index = ic.at(0);
    int cnt = ic.at(1);
    if(index < 0 || index >= nSender) 
    {
        NS_LOG_DEBUG (this << index << ". " << cnt << ": returned.");
        return;
    }
    ProtocolType protocol = ip2prot[getIpSrcAddr(p)];
    txwnd[index] ++;

    ss2 << "MacTX: " << index << ". " << cnt << ": seq = " << (protocol == TCP? getTcpSequenceNo(p):-1) << ", ack = " << (protocol == TCP? getTcpAckNo(p):-1);
    if(isTrackPkt) NS_LOG_DEBUG (ss2.str());
    Acka.insert(p->Copy(), index);

    // for debug only
    if(isTrackPkt)
    {
        stringstream ss;
        ss << " -- onMacTx: " << index << ". " << cnt << ": seq = " 
        << (protocol == TCP? getTcpSequenceNo(p):-1) << ", " << getPktSizes(p, protocol).at(3) << " B";
        
        NS_LOG_DEBUG (ss.str());
    }
}

void
MiddlePoliceBox::onRouterTx(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin. ");
    vector<int> ic = ExtractIndexFromTag(p);
    int index = ic.at(0);
    int cnt = ic.at(1);
    if(index < 0 || index >= nSender) return;
    // NS_LOG_INFO("   - RX router: " + to_string(index) + ". " + to_string(cnt));

}

void 
MiddlePoliceBox::onMacTxDrop(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    vector<int> ic = ExtractIndexFromTag(p);
    int index = ic.at(0);
    int cnt = ic.at(1);
    if(index < 0 || index >= nSender) return;
    txDwnd[index] ++;
    NS_LOG_INFO("txDwnd[" + to_string(index) + "] = " + to_string(txDwnd[index]));
}

void 
MiddlePoliceBox::onPhyTxDrop(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    vector<int> ic = ExtractIndexFromTag(p);
    int index = ic.at(0);
    int cnt = ic.at(1);
    if(index < 0 || index >= nSender) return;
    phyTxDwnd[index] ++;
    NS_LOG_INFO("phyTxDwnd[" + to_string(index) + "] = " + to_string(phyTxDwnd[index]));
}

void 
MiddlePoliceBox::onPhyRxDrop(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    vector<int> ic = ExtractIndexFromTag(p);
    int index = ic.at(0);
    int cnt = ic.at(1);
    if(index < 0 || index >= nSender) return;
    phyRxDwnd[index] ++;

}

//!< all drop for debug
void 
MiddlePoliceBox::onPhyRxDrop2(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    vector<int> ic = ExtractIndexFromTag(p);
    int index = ic.at(0);
    int cnt = ic.at(1);
    if(index < 0 || index >= nSender) return;
    dropWnd[0][index] ++;
}

void 
MiddlePoliceBox::onMacTxDrop2(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    vector<int> ic = ExtractIndexFromTag(p);
    int index = ic.at(0);
    int cnt = ic.at(1);
    if(index < 0 || index >= nSender) return;   
    dropWnd[1][index] ++;

}

void 
MiddlePoliceBox::onPhyTxDrop2(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    vector<int> ic = ExtractIndexFromTag(p);
    int index = ic.at(0);
    int cnt = ic.at(1);
    if(index < 0 || index >= nSender) return;
    dropWnd[2][index] ++;    
}

// debug: trace and obtain the ACK information
void
MiddlePoliceBox::onAckRx(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    Ptr<const Packet> pcp = p->Copy();
    ProtocolType protocol = ip2prot[getIpSrcAddr(p)];
    uint32_t seq = protocol == TCP? getTcpSequenceNo(pcp):1;
    uint32_t ack = protocol == TCP? getTcpAckNo(pcp):1;
    uint16_t win = protocol == TCP? getTcpWin(pcp):1;
    Ipv4Address des = getIpDesAddr(pcp);

    int index = Acka.extract_index(p->Copy());
    if(index < 0 || index >= nSender) return;
    if(isTrackPkt)
    {
        stringstream ss;
        ss << " -- onAckRx: " << index << ": seq = " << seq << "; ack = " << ack;
        NS_LOG_FUNCTION(ss.str());
    }
    if(win == 0) 
    {
        NS_LOG_INFO(" --- Notice: TCP win = 0!!!");
        exit(0);
    }
}


void
MiddlePoliceBox::onMboxAck(Ptr<const Packet> p)
{   
    NS_LOG_FUNCTION("  Begin.  ");
    // if UDP, no need for this
    Ptr<const Packet> pcp = p->Copy();
    vector<int> ic = ExtractIndexFromTag(p);
    int cnt = ic.at(1);
    ProtocolType protocol = ip2prot[getIpDesAddr(p)];

    int ackSize = getPktSizes(p->Copy(), protocol).at(3);


    bool isTcp = protocol == TCP;
    uint32_t seq = isTcp? getTcpSequenceNo(pcp):cnt;
    uint32_t ack = isTcp? getTcpAckNo(pcp):cnt;
    uint16_t win = isTcp? getTcpWin(pcp):1;

    int index = Acka.extract_index(p->Copy());          // avoid no map case, which means no insertion occurs
    if(index < 0 || index >= nSender) return;
  
    if(0)   // test for AckAnalysis part
    {
        stringstream ss1;
        for(uint32_t j = 0; j < 10; j ++)
            if( (isTcp && Acka.update(index, ack)) )     // wrong logic
                ss1 << "  Ack analysis update (" << index << ", " << ack << ") should drop: " << endl;
            else ss1 << "  Ack analysis update (" << index << ", " << ack << ") smoothly: " << endl;
        ss1 << "    - lastNo: " << Acka.get_lastNo(index) << "; times: " << Acka.get_times(index) << endl;
        ss1 << "    - the total Ack: ";
        for(uint32_t j = 0; j < Acka.get_mDropNo(index).size(); j ++)
            ss1 << " " << Acka.get_mDropNo(index).at(j) << ", ";
        // ss1 << endl << "    - map: ";
        // for(auto pr:Acka.get_map()) ss1 << pr.first << " -> " << pr.second << "; ";
        ss1 << endl;
        NS_LOG_INFO(ss1.str());
    }
    else if (isTcp)   // actual functionality
    {
        NS_LOG_FUNCTION("   acka: index = " + to_string(index));
        if( Acka.update(index, ack) )
        {
            lDrop[index] ++;
            isCA[index] = true;
            NS_LOG_INFO("  - update " + to_string(index) + ": ack = " + to_string(ack) + "; size = " + to_string(ackSize));
        }
    }
    else
    {
        uint32_t res = Acka.update_udp_drop(index, ack);
        if (res > 0) 
        {
            lDrop[index] += res;
            NS_LOG_DEBUG("  - update(udp) " + to_string(index) + ": ack = " + to_string(ack) + "; ldrop += " + to_string(res));
        }
    }

    NS_LOG_FUNCTION ("End.");

}

void
MiddlePoliceBox::onSenderTx(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("  Begin.  ");
    vector<int> ic = ExtractIndexFromTag(p);
    NS_LOG_FUNCTION(" - After extraction.");

    int index = ic.at(0);
    int cnt = ic.at(1);
    if(index < 0 || index >= nSender) 
    {
        NS_LOG_FUNCTION(to_string(index) + ". " + to_string(cnt));
        return;
    }
    tcpwnd[index] ++;
}

void
MiddlePoliceBox::onCwndChange(string context, uint32_t oldValue, uint32_t newValue)
{
    NS_LOG_FUNCTION("Begin.");
    size_t pos = context.find("/Congestion");
    uint32_t i = context.substr(pos - 1, 1)[0] - '0';       // get the flow index, need testing

    congWnd[i] = newValue;
    NS_LOG_FUNCTION(" - " + to_string(i) + ": CongWnd = " + to_string(newValue));
    fout.at(i)[5] << Simulator::Now().GetSeconds() << " " << newValue << endl;
}

void
MiddlePoliceBox::onRttChange(string context, Time oldRtt, Time newRtt)
{
    NS_LOG_FUNCTION("Begin.");
    size_t pos = context.find("/RTT");
    uint32_t i = context.substr(pos - 1, 1)[0] - '0';       // get the flow index, need testing
    
    rtt[i] = newRtt.GetSeconds();
    NS_LOG_FUNCTION(" - " + to_string(i) + ": RTT = " + to_string(rtt[i]));
    fout.at(i)[6] << Simulator::Now().GetSeconds() << " " << rtt[i] << endl;
}

void
MiddlePoliceBox::onRxAck(string context, SequenceNumber32 vOld, SequenceNumber32 vNew)
{
    rxAckNo = vNew;
    // NS_LOG_INFO (this << "onRxAck " << vNew.GetValue());
}

void
MiddlePoliceBox::onLatency(string context, Time oldLatency, Time newLatency)
{
    NS_LOG_FUNCTION (this << " onLatency " << newLatency.GetSeconds());
    size_t pos = context.find("/Latency");
    uint32_t i = context.substr(pos - 1, 1)[0] - '0';
    fout.at(i)[18] << Simulator::Now().GetSeconds() << " " << rxAckNo.GetValue() << " " << newLatency.GetSeconds() << endl;
    
}

void
MiddlePoliceBox::onTcpRx (Ptr<const Packet> p)
{
    Ptr<Packet> pcp = p->Copy ();
    vector<int> ic = ExtractIndexFromTag(pcp);
    int tmp = ic.at(0);
    if (tmp == -1) NS_LOG_INFO ("TCP RX: Wrong index!");
    else 
    {
        NS_LOG_INFO ("TCP RX: Current Index: " << tmp);
        curIndex = tmp;
    }
}

void
MiddlePoliceBox::clear()
{
    for(uint32_t i = 0; i < nSender; i ++)
    {
        rwnd[i] = 0;
        // cwnd[i] = 0;
        lDrop[i] = 0;
        qDrop[i] = 0;
        nAck[i] = 0;
        mDrop[i] = 0;

        bePkt[i] = 0;

        // redundant, for debug only
        txwnd[i] = 0;
        rxwnd[i] = 0;
        txDwnd[i] = 0;
        phyTxDwnd[i] = 0;
        phyRxDwnd[i] = 0;

        tcpwnd[i] = 0;
        for(uint32_t j = 0; j < dropWnd.size(); j ++)
            dropWnd[j][i] = 0;
        ackwnd[i] = 0;
    }
    // if((int)(Simulator::Now().GetSeconds() * 20) % 2 == 0) 
    // Acka.clear_seq();   
    NS_LOG_FUNCTION(" - window reset to 0.");
}

void
MiddlePoliceBox::flowControl(FairType fairness, double interval, double logInterval, double ruInterval, Ptr<QueueDisc> tbfq)
{
    cout << "By pass: " << bypassMacRx << endl;
    // schedule
    double rttInterval = interval;
    if(0 && rtt.at(0)) rttInterval = rtt.at( max_element(rtt.begin(), rtt.end()) - rtt.begin() );       // use updated rtt value

    if(!isStop) 
        fEID = Simulator::Schedule(Seconds(rttInterval), &MiddlePoliceBox::flowControl, this, fairness, interval, logInterval, ruInterval, tbfq);

    if(isStatReady)
    {
        sEID = Simulator::Schedule(Seconds(0.01), &MiddlePoliceBox::statistic, this, logInterval);
        cEID = Simulator::Schedule(Seconds(0.01), &MiddlePoliceBox::crossStat, this, logInterval);
        Simulator::Schedule(Seconds(0.01), &MiddlePoliceBox::rateUpdate, this, ruInterval);
        isStatReady = false;
    }
    NS_LOG_FUNCTION(" Begin ... ");

    // update lDrop with qDrop when necessary, and update u_cnt
    for(uint32_t i = 0; i < nSender; i ++)
    {
        // testing update_udp_drop
        // if(protocol == UDP) mDrop[i] = Acka.count_mdrop(i);             // count from the same boundary of lDrop
        totalDrop[i] += lDrop[i];
        mDrop[i] += qDrop[i];                                           // add the part of TBFQ drop
        isCA[i] = lDrop[i] > 0? true:isCA[i];
        // if(bypassMacRx) 
        //     rwnd[i] += lDrop[i];                                     // complement for rwnd ++ on MacTx actually
        u_cnt[i] = lDrop[i] > 0? 0 : u_cnt[i] + 1;
    }

    // wnd data output
    stringstream ss;
    ss << "\n                MID: " << MID << " sm_capacity: " << sm_capacity << endl;
    ss << "flow control:   No.  txwnd  rwnd  ccwnd e-cwnd mdrop ldrop  sm_rwnd  sm_cwnd" << endl;
    for (uint32_t i = 0; i < nSender; i ++)
        ss << "                " << i << "    " << txwnd[i] << "     " << rwnd[i] << "     " << cwnd[i]
        << "     " << floor(sswnd[i]) << "      " << mDrop[i] << "     " << lDrop[i] << "     " << sm_rwnd[i] << "   " << sm_cwnd[i] << "   " << endl;  

    NS_LOG_INFO(ss.str());
    
    // for clear output
    for (uint32_t i = 0; i < nSender; i ++)
    {
        if(lDrop[i] > 0 || mDrop[i] > 0) 
        {
            NS_LOG_INFO("Note the losses here!");
            break;
        }
    }

    // compute EBRC rate
    // loss rate update and capacity calculation: outdated now
    double capacity = 0;
    for(uint32_t i = 0; i < nSender; i ++)
    {
        double lossRate = rwnd[i] > 0? (double) (lDrop[i] + mDrop[i])/rwnd[i] : 0.0;
        llr[i] = rwnd[i] > 5? (1 - beta) * lossRate + beta * llr[i] : beta * llr[i];
        double tmp = rwnd[i] - lDrop[i] - mDrop[i]; 

        capacity += rwnd[i] > lDrop[i] + mDrop[i]? tmp : 0;
    }
    // if (accumulate(lDrop.begin(), lDrop.end(), 0) == 0)
    //     capacity += nSender * 2;                            // if no link drop allow a little exploration

    // compute Priority Loss Rate (PLR)
    double Plr = 0.0;
    vector<double> Ebed(nSender, 0);
    vector<double> Idr(nSender, 0);
    vector<int> Dlvwnd(nSender, 0);
    double BeRatio = 1.0;
    for(uint32_t i = 0; i < nSender; i ++)
    {
        // Dlvwnd[i] = max(0, (int)rwnd[i] - (int)mDrop[i] - (int)lDrop[i]);                      // re-compute delivery rate
        Dlvwnd[i] = rwnd[i] > mDrop[i] + lDrop[i]? rwnd[i] - mDrop[i] - lDrop[i] : 0;
        Plr += max(0, (int)min(cwnd[i], rwnd[i]) - (int)Dlvwnd[i]);      // a little doubtable
    }
    
    // compute EBED_i and EBED: out_dated now
    double t1 = accumulate(rwnd.begin(), rwnd.end(), 0.0) - accumulate(Dlvwnd.begin(), Dlvwnd.end(), 0.0);
    double t2 = 0.0;
    vector<uint32_t> assigned(nSender, 0);
    vector<uint32_t> all_loss(nSender, 0);
    stringstream ss1;
    for(uint32_t i = 0; i < nSender; i ++)
        t2 += rwnd[i] > cwnd[i]? rwnd[i] - cwnd[i]:0;       // compute the sum of BE packets
    if(t2)
    {       // reformat the logging here!
        BeRatio = 1 - t1 / t2;
        ss1 << "PLR = " << Plr << endl;
        ss1 << "BE ratio: " << BeRatio << "; sum(Ai - Di) = " << t1 << "; sum(max(ri, Ai)) = " << t2 << endl;
        NS_ASSERT_MSG(BeRatio <= 1, " EBED drop ratio is not less than 1!");
        ss1 << "Idr/Ebed : ";
        for(uint32_t i = 0; i < nSender; i ++)
        {
            Idr[i] = min(cwnd[i], rwnd[i]) + max(0.0, (double)rwnd[i] - (double)cwnd[i]) * BeRatio; 
            Ebed[i] = max(0.0, Dlvwnd[i] - Idr[i]);          // if Dlvwnd < Idr, then Ebed is 0, which means no drop will be imposed on the flow

            ss1 << Idr[i] << " / " << Ebed[i] << ", ";      // for debug only, need reformat
        }
        ss1 << endl;
        

        double totalEbed = accumulate(Ebed.begin(), Ebed.end(), 0.0);
        if(!totalEbed) totalEbed ++;

        // compute tax for each sender: reset every time (should or not?)
        for(uint32_t i = 0; i < nSender; i ++)
            assigned[i] = floor(Plr * Ebed[i] / totalEbed);
        uint32_t totalTax = accumulate(assigned.begin(), assigned.end(), 0);
        uint32_t restN = (uint32_t)Plr - totalTax;
        assigned = assignRandomLoss(assigned, Ebed, restN);
        if(Plr > 0)
        {
            for(uint32_t j = 0; j < nSender; j ++)
                if(Ebed[j] > 0) all_loss[j] = 1;        // avoid the victim of link drop
                // if(lDrop[j] == 0) all_loss[j] = 1;
        }
    }

    // Latest mechanism: slow long run bandwidth reuse control
    Lrm.updateWnd(rwnd, cwnd, capacity);
    if (counter == lr_period)
        Lrm.setWnd(sm_rwnd, sm_cwnd);
    if (counter ++ % lr_period == 0)
    {
        // Bm.refresh(Lrm.getRwnd(), Lrm.getCtrlCapacity());       // reduce real capacity by 0.75        
        Bm.reuse_switch(Lrm.getRwnd(), Lrm.getCwnd(), Lrm.getCtrlCapacity());
        NS_LOG_INFO("Long-run reuse:");
        Lrm.logging();
        Bm.logging();
    }

    // Current official: bandwidth manager
    double ra = 3.5;
    vector<uint32_t> dwnd(nSender, 0);
    for(uint32_t i = 0; i < nSender; i ++)
    {
        sm_rwnd[i] = sm_rwnd[i] < 1e-3? rwnd[i] : (1 - a)*sm_rwnd[i] + a*rwnd[i];   // update sm_rwnd for refresh
        // sm_rwnd[i] = sm_rwnd[i] < 1e-3 || rwnd[i] > ra * sm_rwnd[i] || rwnd[i] < 1/ra * sm_rwnd[i]? rwnd[i] : (1 - a)*sm_rwnd[i] + a*rwnd[i];
        dwnd[i] = rwnd[i] > lDrop[i] + mDrop[i]? rwnd[i] - lDrop[i] - mDrop[i] : 0;
    }

    // Bm.refresh(sm_rwnd, accumulate(rwnd.begin(), rwnd.end(), 0));
    // Bm.refresh(sm_rwnd, capacity);
    // Bm.refresh(sm_rwnd, sm_capacity);
    // Bm.refresh(Lrm.getRwnd(), Lrm.getCapacity());

    sm_capacity = sm_capacity < 1? capacity : (1 - a) * sm_capacity + a * capacity;
    vector<double> tmp_cwnd(nSender, 0), cur_cwnd(nSender, 0);

    // 0. lr reuse, short-term control, respective to flow type
    for (uint32_t i = 0; i < nSender; i ++)
    {
        if (Bsc.getState(i) != BEOFF)
        {
            // tmp_cwnd[i] = Bm.getTmpCwndByCapacity(sm_capacity)[i];          // lr ratio * sm capacity
            tmp_cwnd[i] = max(Bm.getTmpCwndByCapacity(Lrm.getCtrlCapacity())[i], Bm.getTmpCwndByCapacity(sm_capacity)[i]);
            cur_cwnd[i] = tmp_cwnd[i];
        }
        else        // use mbox to drop UDP flow, so add 0.75 here
        {
            tmp_cwnd[i] = max(Bm.getTmpCwndByCapacity(Lrm.getCtrlCapacity())[i], Bm.getTmpCwndByCapacity(sm_capacity)[i]);    // make it smoother
            cur_cwnd[i] = tmp_cwnd[i];
        }
    }

    // 1. no reuse involved
    // tmp_cwnd = Bm.getWeiCwnd();
    // cur_cwnd = Bm.getWeiCwnd();

    // 2. example of sm / lr reuse
    // tmp_cwnd = Bm.getTmpCwnd ();
    // cur_cwnd = Bm.getCurCwnd ();
    //     Bm.refresh(Lrm.getRwnd(), Lrm.getCapacity());       // not verified
    //     tmp_cwnd = Bm.getTmpCwndByCapacity(capacity);
    //     cur_cwnd = Bm.getCurCwndByCapacity(capacity);

    // Debug: print tmp_cwnd for all flows here






    // latest: real-time rate based control, using smoothed rwnd/cwnd (should before cwnd update)
    vector<double> sm_RCR(nSender, 0);
    for(uint32_t i = 0; i < nSender; i ++)
    {
        // sm_cwnd[i] = sm_cwnd[i] < 1e-3 || cur_cwnd[i] > ra*sm_cwnd[i] || cur_cwnd[i] < 1/ra * sm_cwnd[i]? 
            // cur_cwnd[i] : (1 - a)*sm_cwnd[i] + a*cur_cwnd[i];
        sm_cwnd[i] = sm_cwnd[i] < 1e-3? cur_cwnd[i] : (1 - a)*sm_cwnd[i] + a*cur_cwnd[i];
        sm_RCR[i] = (sm_rwnd[i] + 2) / sm_cwnd[i];
    }

    // Mine: refresh weight array
    uint32_t total_rwnd = accumulate(rwnd.begin(), rwnd.end(), 0);
    bool isAtStart = safe_count > 0.7 * Simulator::Now().GetSeconds() / interval;

    // update memory rate we want: compare to rwnd in this period
    vector<double> oldMwnd(mwnd);
    for(uint32_t i = 0; i < nSender; i ++)
    {
        if(mDrop[i] + lDrop[i] == 0) mwnd[i] = rwnd[i];
        else mwnd[i] = rwnd[i] * pow(2.0/3, (double) mDrop[i] + lDrop[i]);
    }

    // set e_cwnd for retransmission timeout detection
    for(uint32_t i = 0; i < nSender; i ++)
        if(mwnd[i] < 2 && oldMwnd[i] < 2) sswnd[i] = 10;

    // check the total loss
    uint32_t drop_sum = accumulate(lDrop.begin(), lDrop.end(), 0);  // if drop_sum == 0, then don't control by relative rate
    if(drop_sum == 0) safe_count ++;
    else safe_count = 0;
    if(safe_count > 0.75 * safe_Th) cout << "safe count: " << safe_count << "; inte's: " << Simulator::Now().GetSeconds() / interval << endl;

    // update drop request and call soft control
    vector<int> dropReq(nSender, false);
    for(uint32_t i = 0; i < nSender; i ++)
    {
        // if(mwnd[i] < rwnd[i] / 4.0 && oldMwnd[i] < rwnd[i] / 4.0)   // BE off
        //     dropReq[i] = 2;         
        if (rwnd[i] > sswnd[i])
            dropReq[i] = 4;
        // else if (tax[i] > 0)                                        // mild drop: compatible with 2nd interval tax
        else if (sm_rwnd[i] > b*sm_cwnd[i] && safe_count < safe_Th)          // b defined previously
            dropReq[i] = 3;
        else dropReq[i] = 0;                                        // back to BE on 
    }

    // Current official: refresh the slow start module
    vector<bool> if_end(nSender, false);
    vector<double> tmp_weight(nSender, 0);
    double total_cwnd = accumulate(tmp_cwnd.begin(), tmp_cwnd.end(), 0);
    total_cwnd = total_cwnd == 0? total_cwnd + 1 : total_cwnd;
    for(uint32_t i = 0; i < nSender; i ++)
    {
        tmp_weight[i] = tmp_cwnd[i] / total_cwnd;
    }
    ssDrop = vector<bool> (nSender, false);

    // cout << "u_cnt of flows: ";
    // for(auto c:u_cnt) cout << c << " ";
    // cout << endl;
    
    // update CA before dwnd of Ssm is updated
    vector<uint32_t> last_dwnd = Ssm.get_last_dwnd();
    vector<bool> ssm_isSS = vector<bool> (nSender, false);
    for(uint32_t i = 0; i < nSender; i ++)
    {
        if (last_rwnd[i] == 0 && rwnd[i] == 0)          // for UDP case
        // if(last_dwnd[i] == 0 && dwnd[i] == 0)       // need testing!
        {
            isCA[i] = false;
            ssm_isSS[i] = true;
            sswnd[i] = 10;
            sm_rwnd[i] = rwnd[i];       // debug usage
        }
    }

    // update SlowstartManager
    // bool mDropClean = accumulate(mDrop.begin(), mDrop.end(), 0) > 0;
    bool mDropClean = accumulate(mDrop.begin(), mDrop.end(), 0) == 0;       // no mDrop
    
    bool ini_safe =  mDropClean && safe_count > 0.8 * (Simulator::Now().GetSeconds() / interval);
    uint32_t clean_count =  mDropClean? safe_count : 0;       // except the mdrop case

    vector<bool> ifOld = Ssm.refresh(dwnd, clean_count, u_cnt, ssm_isSS, if_end, ssDrop, tmp_weight, ini_safe);
    for(uint32_t i = 0; i < nSender; i ++)
    {    
        if(if_end[i])
        {
            sm_rwnd[i] = rwnd[i];
            sm_cwnd[i] = tmp_cwnd[i];       // trial
        }
        // cout << if_end[i] << " " ;
    }
    // cout << endl;

    // cout << " Slow start drop or not: ";
    // copy(ssDrop.begin(), ssDrop.end(), std::ostream_iterator<bool>(std::cout, " "));
    // cout << endl;
    // Ssm.print();


    for(uint32_t i = 0; i < nSender; i ++)
        if(!ifOld[i]) dropReq[i] = 0;                               // if no drop yet, don't control
        else if (ssDrop[i]) dropReq[i] = 3;

    // debug: output the result of drop request
    // cout << "   - initial safety: " << ini_safe << endl;
    // for (uint32_t i = 0; i < nSender; i ++)
    //     cout << "   - dropReq[" << i << "]: " << dropReq[i] << endl;
    

    // Bsc.update(dropReq, sm_rwnd, sm_cwnd);                         // it's also smoothed rwnd that should be used in log2 calculation
    vector<double> d_rwnd(rwnd.begin(), rwnd.end());
    // Bsc.update(dropReq, d_rwnd, tmp_cwnd);                            // in case smoothing update doesn't work, e.g. slow start
    vector<double> d_cwnd(cwnd.begin(), cwnd.end());            // use new cwnd for updating
    // Bsc.update(dropReq, d_rwnd, d_cwnd, safe_count);            // tmp_cwnd is cwnd of next interval, here should use cwnd?
    // Bsc.update(dropReq, d_rwnd, d_cwnd, u_cnt);

    vector<uint32_t> pure_sc (nSender, safe_count);
    Bsc.update(dropReq, d_rwnd, d_cwnd, pure_sc); 
    // Bsc.print();
    
    // debug e_cwnd

    for (uint32_t i = 0; i < nSender; i ++)
    {
        if (isCA[i]) cout << i << " is in CA, ";
        if(!(last_lDrop[i] <= 3 && last_lDrop[i] > 0))  // if last_ldrop is in [1,x], then it might be in recovery mode
        {
            if (mDrop[i] + lDrop[i] > 0)
            {
                sswnd[i] = sswnd[i] / 2.0 + 10;
                cout << "e_cwnd halves." << endl;
            }
        }
        if (isCA[i])      
            // sswnd[i] += rtt[i] > 0? (double) interval * rho / rtt[i]: 3 * rho;     // emulating: #ack = (ip pkt_size)/( tcp pkt_size) * interval / rtt
            sswnd[i] += rtt[i] > 0? (int) (interval / rtt[i] + 1) * rho : 3 * rho;
    }
    last_lDrop = lDrop;
    last_rwnd = rwnd; 

    // update cwnd after display: now put at the last
    // flow control policy based on fairness: add cases to extend more fairness
    for(uint32_t i = 0; i < nSender; i ++)
    {
        switch(fairness)
        {
            case NATURAL:
                cwnd[i] = rwnd[i] > lDrop[i] + mDrop[i]? rwnd[i] - lDrop[i] - mDrop[i] : 0;
                break;
            case PERSENDER:
                cwnd[i] = floor(capacity / (double)nSender);
                break;
            case PRIORITY:
                cwnd[i] = tmp_cwnd[i];
                // cwnd[i] = floor(capacity * rt_weight[i]);      // don't directly use weight[i]
                // cwnd[i] = floor(capacity * weight[i]);          // just tested!!!
                break;
            default: ;
        }
    }

    // add up the tcp
    uint32_t totalTcp = 0;
    for (uint32_t i = 0; i < nSender; i ++)
        totalTcp += tcpwnd.at(i);
    singleFout.at(2) << Simulator::Now().GetSeconds() << " " << totalTcp << endl;

    // output log for rwnd, drop window (mdrop + ldrop)
    for (uint32_t i = 0; i < nSender; i ++)
    {
        // fout.at(i)[2] << Simulator::Now().GetSeconds() << " " << lDrop.at(i) + mDrop.at(i) << endl;
        // fout.at(i)[3] << Simulator::Now().GetSeconds() << " " << rwnd.at(i) << endl;
        // fout.at(i)[4] << Simulator::Now().GetSeconds() << " " << tcpwnd.at(i) << endl;
        // fout.at(i)[9] << Simulator::Now().GetSeconds() << " " << mDrop.at(i) << endl;
        // fout.at(i)[10] << Simulator::Now().GetSeconds() << " " << lDrop.at(i) << endl;        

        // test lr capacity
        singleFout.at(7) << Simulator::Now().GetSeconds() << " " << Lrm.getCapacity() << endl;

        // for rate & drop figure only
        fout.at(i)[11] << Simulator::Now().GetSeconds() << " " << mDrop.at(i) * scale + 3*i*0.05*scale << endl;      // scale should be bandwidth / 6
        fout.at(i)[12] << Simulator::Now().GetSeconds() << " " << lDrop.at(i) * scale + (3*i + 1)*0.05*scale << endl;

        // for sswnd debug
        fout.at(i)[13] << Simulator::Now().GetSeconds() << " " << sswnd.at(i) << endl;

        // for smoothing test
        // fout.at(i)[7] << Simulator::Now().GetSeconds() << " " << sm_rwnd.at(i) << endl; 
        // fout.at(i)[8] << Simulator::Now().GetSeconds() << " " << sm_cwnd.at(i) << endl;

        // for return scheme debug: plot weighted cwnd, tmp_cwnd, cur_cwnd in one figure
        // fout.at(i)[14] << Simulator::Now().GetSeconds() << " " << tmp_cwnd.at(i) << endl;
        // fout.at(i)[15] << Simulator::Now().GetSeconds() << " " << cur_cwnd.at(i) << endl;
        // fout.at(i)[16] << Simulator::Now().GetSeconds() << " " << floor(capacity * weight[i]) << endl;
    }

    // ------------------- set TBFQ rate: commented in ns-3.27, which doesn't have Tbf queue ----------------
    // Ptr<FqTbfQueueDisc> fqp = DynamicCast<FqTbfQueueDisc>(tbfq);
    // double coef = 1.5;          // factor to reduce the whole queue
    // cout << endl;
    // if(!is_monitor)
    // for (uint32_t i = 0; i < nSender; i ++)
    // {
    //     bool ifSetTokenRate = Bsc.getState(i) == BEOFF;                         // suitable condition needs testing
    //     double tWnd = ifSetTokenRate? max(Bsc.getTokenCapacity(i), (uint32_t)tmp_cwnd[i]) : 20000;        // rwnd[i] or no restriction? needs testing
    //     // double tWnd = ifSetTokenRate? Bsc.getTokenCapacity(i) : rwnd[i];
    //     string newTokenRate = to_string (int(tWnd * 1400 * coef * normSize / interval)) + "kbps";
    //     cout << "  -- Token Wnd: " << (int)tWnd << ", new token rate: " << newTokenRate << endl;
    //     fqp->SetTokenRate (index2des[i], newTokenRate);
    // }

    clear();
}

void
MiddlePoliceBox::statistic(double interval)
{
    // static vector<uint32_t> lastRx2 = vector<uint32_t>(nSender, 0);
    if(!isStop) 
        sEID = Simulator::Schedule(Seconds(interval), &MiddlePoliceBox::statistic, this, interval);

    NS_LOG_FUNCTION(" Begin, nSender: " + to_string(nSender));

    // output the data rate    
    for(uint32_t i = 0; i < nSender; i ++)
    {
        dRate[i] = (double)(totalRxByte[i] - lastRx2[i]) * normSize / interval;     // record rx data rate
        fout.at(i)[0] << Simulator::Now().GetSeconds() << " " << dRate.at(i) << " kbps" << endl; 
        lastRx2[i] = totalRxByte[i];                                                // record llr
        fout.at(i)[1] << Simulator::Now().GetSeconds() << " " << llr.at(i) << endl;     
        
        // output RTT & LLR for auto-encoder data
        fout.at(i)[17] << Simulator::Now().GetSeconds() << " " << rtt[i] << " " << llr[i] << endl;
    }

    double actual_weight = dRate[1] / accumulate(dRate.begin(), dRate.end(), 0.0);      // have bias, short term throughput seems more reasonable

    singleFout.at(0) << Simulator::Now().GetSeconds() << " " << slr << endl;      // record slr
    singleFout.at(5) << Simulator::Now().GetSeconds() << " " << actual_weight << endl;

    // record Queue size: later version feature
    

}

void
MiddlePoliceBox::crossStat(double interval)
{
    if(!isStop)
        cEID = Simulator::Schedule(Seconds(interval), &MiddlePoliceBox::crossStat, this, interval);
    NS_LOG_FUNCTION(" Begin. ");
    vector<double> drate(nCross);
    for(uint32_t i = 0; i < nCross; i ++)
    {
        drate[i] = (double)(totalCrossByte[i] - lastCross[i]) * normSize / interval;
        fout[nSender + i][0] << Simulator::Now().GetSeconds() << " " << drate[i] << " kbps" << endl;
        lastCross[i] = totalCrossByte[i];
    }

}

void
MiddlePoliceBox::rateUpdate(double interval)
{
    Simulator::Schedule(Seconds(interval), &MiddlePoliceBox::rateUpdate, this, interval);

    NS_LOG_FUNCTION("Begin.");
    double beta = alpha / 10;
    for(uint32_t i = 0; i < nSender; i ++)
    {
        // tx rate update
        double newTxRate = (double)(totalTxByte[i] - lastTx[i]) * normSize / interval;
        lastTx[i] = totalTxByte[i];
        txRate[i] = (1 - alpha) * txRate[i] + alpha * newTxRate;        // moving average
        NS_LOG_FUNCTION("  - " + to_string(i) + " : New tx rate = " + to_string(newTxRate) + 
            " kbps; tx rate = " + to_string(txRate[i]));
    }

}

void MiddlePoliceBox::stop()
{
    isStop = true;
    isStatReady = true;
    Simulator::Cancel(sEID);
    Simulator::Cancel(fEID);
    Simulator::Cancel(cEID);

    NS_LOG_INFO("Stop mbox now.");
}

void MiddlePoliceBox::start()
{
    isStop = false;
    NS_LOG_INFO("Start mbox now.");
}

FairType MiddlePoliceBox::GetFairness()
{
    return fairness;
}

void MiddlePoliceBox::SetWeight(vector<double> w)
{
    NS_ASSERT(w.size() >= nSender);
    for(int i = 0; i < nSender; i ++)
        weight[i] = w.at(i);
    Bm = BandwidthManager(weight);
}

void MiddlePoliceBox::SetRttRto(vector<double> rtt)
{
    NS_ASSERT_MSG(rtt.size() == nSender, "Input RTT array has a wrong size!");
    RTT = rtt;
    if(tRto.empty()) tRto = vector<double> (nSender, 0);
    for(uint32_t i = 0; i < rtt.size(); i ++)
        tRto[i] = RTT[i];                      // according to Hu, it's bound of tRto
}

vector<int> MiddlePoliceBox::ExtractIndexFromTag(Ptr<const Packet> p)
{
    Ptr<const Packet> pcp = p->Copy();
    MyTag tag;
    if(!pcp->PeekPacketTag(tag)) return {-1, -1};

    MyApp temp;
    uint32_t tagScale = temp.tagScale;
    
    if(!isTrackPkt)
        return {(int)tag.GetSimpleValue() - 1, -1};         // 1. normal case index = value - 1
    else
    {
      int index = tag.GetSimpleValue () / tagScale - 1;     // 2. track each packet
      int cnt = tag.GetSimpleValue () % tagScale;
      return {index, cnt};
    }
}

}

// int
// main()
// {
//     double tStop = 5.0;
//     double interval = 1.0;
//     double logInterval = 1.0;
//     ProtocolType ptt = UDP;
//     string Protocol = ptt == TCP? "ns3::TcpSocketFactory":"ns3::UdpSocketFactory";
//     uint32_t port = 8080;
//     string bw = "10Mbps";
//     string txRate = "100Mbps";
//     LogComponentEnable("MiddlePoliceBox", LOG_LEVEL_INFO);    // see function log to test
//     // LogComponentEnable("PointToPointNetDevice", LOG_FUNCTION);

//     NodeContainer n1;
//     n1.Create(2);
//     PointToPointHelper p2p;
//     p2p.SetChannelAttribute("Delay", StringValue("2ms"));
//     p2p.SetDeviceAttribute("DataRate", StringValue(bw));
//     NetDeviceContainer dev = p2p.Install(n1);
//     InternetStackHelper ish;
//     ish.Install(n1);

//     // queue setting
//     QueueDiscContainer qc;
//     TrafficControlHelper tch1;
//     // tch1.Uninstall(dev);
//     tch1.SetRootQueueDisc("ns3::RedQueueDisc",
//                         "MinTh", DoubleValue(5),
//                         "MaxTh", DoubleValue(25),
//                         "LinkBandwidth", StringValue(bw),
//                         "LinkDelay", StringValue("2ms"));
//     qc = tch1.Install(dev);
    
//     Ipv4AddressHelper idh;
//     idh.SetBase("10.1.1.0", "255.255.255.0");
//     Ipv4InterfaceContainer ifc = idh.Assign(dev);
    
//     // sink app
//     Address sinkLocalAddr (InetSocketAddress (Ipv4Address::GetAny (), port));
//     ApplicationContainer sinkApp;
//     PacketSinkHelper psk(Protocol, sinkLocalAddr);
//     sinkApp = psk.Install(n1.Get(1));
//     sinkApp.Start(Seconds(0.0));
//     sinkApp.Stop(Seconds(tStop));

//     // tx application
//     TypeId tid = ptt == TCP? TcpSocketFactory::GetTypeId():UdpSocketFactory::GetTypeId();
//     Ptr<Socket> sockets = Socket::CreateSocket(n1.Get(0), tid);
//     Address sinkAddr(InetSocketAddress(ifc.GetAddress(1), port));
//     Ptr<MyApp> app = CreateObject<MyApp> () ;
//     app->SetTagValue(1);
//     app->Setup(sockets, sinkAddr, 1000, DataRate(txRate));
//     n1.Get(0)->AddApplication(app);
//     app->SetStartTime(Seconds(0));
//     app->SetStopTime(Seconds(tStop));

//     // test process here
//     MiddlePoliceBox mbox1(vector<uint32_t>{1,1,1,1}, tStop, ptt, 0.9);
//     mbox1.install(dev.Get(0));
//     cout << " Mbox Intalled. " << endl << endl;

//     // trace: need test, example online is: MakeCallback(&function, objptr), objptr is object pointer
//     dev.Get(0)->TraceConnectWithoutContext("MacTx", MakeCallback(&MiddlePoliceBox::onMacTx, &mbox1));
//     dev.Get(1)->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onPktRx, &mbox1));
//     qc.Get(0)->TraceConnectWithoutContext("Drop", MakeCallback(&MiddlePoliceBox::onQueueDrop, &mbox1));

//     // begin flow control
//     mbox1.flowControl(PERSENDER, interval, logInterval);

//     // routing
//     Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

//     // stop & run
//     cout << " Running ... " << endl << endl;
//     Simulator::Stop(Seconds(tStop));
//     Simulator::Run();

//     cout << " Destroying ... " << endl << endl;
//     Simulator::Destroy();

//     return 0;
// }