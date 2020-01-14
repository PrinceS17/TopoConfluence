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


#ifndef MBOX_H
#define MBOX_H

#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

#include "ns3/point-to-point-layout-module.h"
#include "ns3/rtt-estimator.h"
#include "ns3/nstime.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/random-variable-stream.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/traffic-control-module.h"
#include "ns3/point-to-point-net-device.h"

#include "ns3/tag.h"
#include "ns3/minibox.h"
#include "ns3/tools.h"
#include "apps.h"

#include <iomanip>
#include <fstream>
#include <ctime>
#include <locale>
#include <time.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <list>
#include <numeric>
#include <algorithm>
#include <map>
#include <limits>
#include <cstdio>
#include <stdlib.h>

using namespace std;
using namespace ns3;

namespace ns3 {

class LongRunSmoothManager
{
public:
    LongRunSmoothManager () {};
    LongRunSmoothManager (uint32_t N, double beta=0.005, uint32_t M = 50);
    void updateWnd (vector<uint32_t> rwnd, vector<uint32_t> cwnd, double capacity);
    void setWnd (vector<double> sm_rwnd, vector<double> sm_cwnd);
    vector<double> getRwnd ();
    vector<double> getCwnd ();
    double getCapacity();
    double getCtrlCapacity();
    void logging();

private:
    uint32_t N;
    uint32_t M;     // length of capacity window
    uint32_t counter;   // count for max capacity
    double beta;
    vector<double> lr_rwnd;         // long-run rwnd: ~ 2 TCP peak-to-peak
    vector<double> lr_cwnd;         // long-run cwnd
    double lr_capacity;             // long-run capacity
    list<double> capa_wnd;        // array for recent capacities
    vector<list<double>> cwnds;
    vector<list<double>> rwnds;
};

class SlowstartManager
{
public:
    SlowstartManager() {};
    SlowstartManager(vector<double> weight, uint32_t safe_Th);
    /**
     * Update the bool arrays of if using rate-based control (old control), if in SS stage,
     * if squeeze in next interval and the last delivery window.
     * 
     * \param dwnd Delivery window of each flow.
     * \param safe_count # intervals passed since the last link drop.
     * \param u_cnt # intervals for each flow from the last link drop.
     * \param if_end Whether the flow should exit SS.
     * \param ssDrop Whether the flow should be dropped.
     * \param weight Temp weight array.
     * \param initial_safe Whether no link drop occurs so far in the simulation.
     *
     * \return ifOld array, i.e., if we begin to control the flow.
     */
    vector<bool> refresh(vector<uint32_t> dwnd, uint32_t safe_count, vector<uint32_t> u_cnt, vector<bool> isSS, vector<bool> &if_end, vector<bool> &ssDrop, vector<double> weight = vector<double>(), bool initial_safe = false);
    vector<uint32_t> get_last_dwnd();
    void print();

private:
    uint32_t N;                     // # sender
    uint32_t safe_Th;               // previously 50, currently 80
    vector<double> weight;          // temp weight
    vector<bool> ifOld;             // if use the old (long-run) control
    vector<bool> isSlowstart;       // if the flow is in Slow Start stage
    vector<uint32_t> last_dwnd;     // delivery window
    vector<bool> slowDrop;          // storage for slow start drop
};

class BandwidthManager
{
public:
    BandwidthManager() {};
    BandwidthManager(vector<double> weight): weight(weight), N(weight.size()), surplus(0), capacity(0), margin(0.1)
    {
        lowTh = 0.6;
        highTh = 0.9;
        tmp_cwnd = vector<double> (N, 0);
        cur_cwnd = vector<double> (N, 0);
        wei_cwnd = vector<double> (N, 0);
    };
    /**
     * Refresh and return tmp_cwnd value by re-allocation without changing the 
     * weight itself, given sm_rwnd and cwnd. Also update the cur_cwnd for smoothing.
     * 
     * \param sm_rwnd Smoothed rwnd.
     * \param cwnd Previous allocated cwnd.
     * \return The tmp cwnd for next time interval.
     */ 
    void refresh(vector<double> sm_rwnd, double capacity);
    void iter_realloc(vector<double> &sm_rwnd, vector<double> &cur_cwnd, vector<bool> &under_user);
    /**
     * Reuse control based on long-run rwnd, cwnd and capacity. Adaptively find a suitable
     * bandwidth for any flow. Tmp cwnd is also used and shall not be modified during two
     * adjacent call of reuse_switch.
     * 
     * \param lr_rwnd Long-run rwnd.
     * \param lr_cwnd Long-run cwnd.
     * \param capacity Long-run capacity.
     */
    void reuse_switch(vector<double> lr_rwnd, vector<double> lr_cwnd, double capacity);
    vector<double> getTmpCwnd();
    vector<double> getCurCwnd();
    vector<double> getWeiCwnd();
    vector<double> getTmpCwndByCapacity(double capacity);
    vector<double> getCurCwndByCapacity(double capacity);
    void logging();

private:
    uint32_t N;
    double capacity;
    double surplus;
    double margin;
    double lowTh;
    double highTh;
    vector<double> weight;
    vector<double> tmp_cwnd;    // cwnd for actual flow control
    vector<double> cur_cwnd;    // cwnd value used for smoothing (>= tmp_cwnd)
    vector<double> wei_cwnd;    // weighted cwnd for original use
};

class WeightManager     // deprecated
{
public:
    WeightManager() {};
    WeightManager(vector<double> weight, double c = 0.5);
    /**
     * Refresh in every time interval to update weight array, in order to detect
     * underusing flow immediately.
     * 
     * \param sm_RCR Smoothed rwnd-cwnd ratio, used for underusing condition.
     * \param lDrop Link drop in this time interval.
     * \param total_rwnd Sum of rwnd in current interval, used in regular update.
     * \return The real-time weight array for next time interval.
     */ 
    vector<double> refresh(vector<double> sm_RCR, vector<uint32_t> lDrop, uint32_t total_rwnd, bool exempt = false);
    vector<uint32_t> get_ucnt() {return u_cnt;}

private:
    uint32_t N;                 // # sender
    double u_Th = 0.667;        // threshold for underusing
    double c;                   // multiplying factor
    vector<uint32_t> u_cnt;     // under count: counts that the flow lasts since last link drop
    vector<double> w0;          // original weight array, save for later comparison
    vector<double> w1;          // real time weight array that is used in each interval

};

class BeSoftControl
{
public:
    BeSoftControl() {};
    BeSoftControl(uint32_t nSender);

    void update(vector<int> dropReq, vector<uint32_t> rwnd, vector<uint32_t> cwnd, vector<uint32_t> safe_count);     //!< update the state and max # drop at the end of each window
    void update (vector<int> dropReq, vector<double> rwnd, vector<double> cwnd, vector<uint32_t> safe_count);
    vector<uint32_t> computeDMax(vector<uint32_t> rwnd, vector<uint32_t> cwnd);         //!< called by update, return the max # drop by iteration
    bool gradDropCond(vector<uint32_t> mDrop, uint32_t i);                              //!< return if drop the pkt, if true, allow to drop flow i's pkt
    bool probDropCond(uint32_t i);
    void print();                                                                       //!< for test, display state[i] and dMax[i]
    uint32_t getNSender();
    BeState getState(uint32_t i);
    uint32_t getTokenCapacity(uint32_t i);        //!< return the capacity (rwnd - dMax) for next interval's token rate

public:
    double explStep;

private:
    uint32_t nSender;
    vector<BeState> state;      // 3 states of the soft control
    vector<uint32_t> dMax;      // max # Drop for each flow
    vector<uint32_t> last_rwnd; // last interval's rwnd
};

// class AckAnalysis
// {
// public:
//     AckAnalysis () {};
//     AckAnalysis (uint32_t n);                           //!< use nSender to initialize all paramters
//     bool insert(Ptr<Packet> p, uint32_t index);         //!< obtain IP destination and insert to the addr2index map, return false if failed
//     int extract_index(Ptr<Packet> p);                   //!< extract index from addr2index, return -1 if failed
//     void insert_pkt(uint32_t i, uint32_t No);        //!< insert sent pkt seq No. into seqNo table
//     void push_back(uint32_t i, uint32_t No);            //!< push back mDrop seq No to mDrop table
//     bool update(uint32_t i, uint32_t No);               //!< update last ack No and times, return false if failed
//     bool update_udp(uint32_t i, uint32_t No);           //!< update the ack No for UDP flows, return true if ack is among rwnd
//     uint32_t update_udp_drop(uint32_t i, uint32_t No);  //!< update the last ack No and return drop number
//     uint32_t count_mdrop(uint32_t i);                   //!< count the mdrop for flow i
//     bool clear(uint32_t i, uint32_t No);                //!< clear mDrop No table in case there are too many entries < No given
//     void clear_seq();                                   //!< clear seqNo table at the end of each interval

//     uint32_t get_lastNo(uint32_t i);
//     uint32_t get_times(uint32_t i);
//     vector<uint32_t> get_mDropNo(uint32_t i);
//     map<uint32_t, uint32_t> get_map();


// private:
//     uint32_t nSender;
//     vector< vector<uint32_t> > mDropNo;         //!< mDrop No. table to cancel later record
//     vector< vector<uint32_t> > seqNo;           //!< seq No.. table for acks
//     vector<uint32_t> startNo;                   //1< start Ack No. for current lDrop session
//     vector<uint32_t> lastNo;                    //!< last Ack No.
//     vector<uint32_t> times;                     //!< appearing times of ack
//     map<uint32_t, uint32_t> addr2index;         //!< map from address to index

// };

class MiddlePoliceBox
{
public:
    /**
     * \brief Initialize the rwnd, cwnd and start monitoring and control.
     * 
     * \param num The array of nSender, nReceiver, nClient and nAttacker.
     * \param tStop The stop time of the simulation.
     * \param prot The protocol type, e.g. TCP, UDP.
     * \param fairn The fairness type, e.g. persender, natural, priority.
     * \param trackPkt Flag if enables the tracking of packet.
     * \param beta Average parameter in llr calculation.
     * \param th Loss rate threshold.
     * \param wnd Collecting wnd of SLR, counts in packets.
     * \param isEDrop If enable SetEarlyDrop of the net device.
     */
    MiddlePoliceBox (vector<uint32_t> num, double tStop, ProtocolType prot, FairType fairn, double pktSize, bool trackPkt = false, 
            double beta = 0.8, vector<double> th = {0.05, 0.05}, uint32_t MID = 0, uint32_t wnd = 50, vector<bool> fls = {true, false, true, false}, double alpha = 0.00004, double scale = 10e3,  double explStep = 50, bool isEDrop = true);
    MiddlePoliceBox () {}
    /** 
     * rewrite to construct the ofstream manually to fullfill the 
     * copy-constructible requirement. 
     * */
    MiddlePoliceBox (const MiddlePoliceBox & );
    MiddlePoliceBox & operator = (const MiddlePoliceBox &);
    ~MiddlePoliceBox ();
    

    friend bool operator == (const MiddlePoliceBox& lhs, const MiddlePoliceBox& rhs)
    { return lhs.MID == rhs.MID; }
    friend bool operator != (const MiddlePoliceBox& lhs, const MiddlePoliceBox& rhs)
    { return lhs.MID != rhs.MID; }
    /**
     * \brief Install this mbox to given net device.
     * 
     * \param NetDevice The device that we install mbox in.
     */
    void install(Ptr<NetDevice> device);
    /**
     * \brief Install the mbox with tx router's rx side to set early drop at MacRx.
     * 
     * \param nc The rx net devices of the tx router.
     */
    void install(NetDeviceContainer nc);
    /**
     * \brief Mbox control drop. Note here ensure the packet will be dropped but will
     * not drop the flow twice.
     * 
     * \param index The flow index that we want to drop.
     * \param seqNo The seq No. of packet for the update of ACK analysis module.
     */
    void controlDrop(uint32_t index, uint32_t seqNo);
    /**
     * \brief Assign the loss to the best-effort flow which has the lowest value.
     * 
     * \param index The flow index that is being processed.
     * \return The drop index, -1 if not success (error occurs).
     */
    int assignLoss(uint32_t index);
    /**
     * \brief This method is called when a packet arrives and is ready to send.
     * In this method, receive window of mbox will be updated and it will set
     * early drop according to the loss rate.
     * NOTE: no need for onMacTxDrop since mDrop is updated here.
     * 
     * \param packet The packet arrived.
     */
    void onMacTx(Ptr<const Packet> p);
    /**
     * \brief This method is called when a packet arrives from all channels.
     * In this method, receive window of mbox will be updated and it will set
     * early drop according to the loss rate.
     * NOTE: no need for onMacTxDrop since mDrop is updated here.
     * 
     * \param packet The packet arrived.
     */
    void onMacRx(Ptr<const Packet> p);
    /**
     * \brief Called similar to mac tx except the mbox is paused and thus doesn't drop 
     * packet to control the flow.
     * 
     * \param packet The packet arrived.
     */
    void onMacRxWoDrop(Ptr<const Packet> p);
    /**
     * \brief Method is called when a packet is dropped by queue (e.g. RED), which 
     * means the link is dropping packet. So # link drop (lDrop) will be updated here.
     * 
     * \param packet The packet dropped.
     */
    void onQueueDrop(Ptr<const QueueDiscItem> qi);

    void onRedDrop(Ptr<const QueueDiscItem> qi);
    /**
     * \brief Called when a packet is dropped by the RX router (represent the link drop).
     * 
     * \param packet The packet dropped.
     */
    void onLinkDrop(Ptr<const Packet> p);
    /**
     * \brief Method is called when a packet is received by destination (e.g. dst router).
     * # RX packet (rwnd) will be updated. And also update SLR when hitting slrWnd (periodically
     * with # RX packet but not time). 
     * 
     * \param packet The packet received by destination.
     */
    void onPktRx(Ptr<const Packet> p);
    /**
     * \brief Trace the change of congestion wnd for each flow.
     * 
     * \param context String to specify the id of trace source.
     * \param oldValue Old cwnd value.
     * \param newValue New cwnd value.
     */
    void onCwndChange(string context, uint32_t oldValue, uint32_t newValue);
    /**
     * \brief Trace the change of RTT for each flow.
     * 
     * \param context String to specify the id of trace source.
     * \param oldValue Old rtt value.
     * \param newValue New rtt value.
     */
    void onRttChange(string context, Time oldRtt, Time newRtt);
    void onRxAck (string context, SequenceNumber32 vOld, SequenceNumber32 vNew);
    void onLatency (string context, Time oldLatency, Time newLatency);
    void onTcpRx (Ptr<const Packet> p);
    /**
     * \brief Reset all the window value to start a new detect period.
     */
    void clear();
    /**
     * \brief Method is called to periodically monitor drop rate and control the flow.
     * Therefore, long term loss rate (llr) is calculated and cwnd's are updated here.
     * rwnd, lDrop, mDrop, cwnd are all refreshed at last here.
     * It also starts of all mbox functionality.
     * 
     * \param fairness Fairness set in prior, e.g. per-sender share fair.
     * \param interval Time interval between this and next calls, i.e. detect period.
     * \param logInterval Time interval between 2 adjacent records of statistics.
     * \param ruInterval Time interval between 2 adjacent tx & Ebrc rate updates.
     * \param Ptr<QueueDisc> TBF queue for token rate adjustment.
     */
    void flowControl(FairType fairness, double interval, double logInterval, double ruInterval, Ptr<QueueDisc> tbfq);
    /**
     * \brief Method is called to periodically update and output statistics, such as 
     * data rate, drop rate of all the links' good put for visualization and analysis.
     * 
     * \param interval Time interval between this and next data collection.
     */
    void statistic(double interval);

    void crossStat(double interval);    //!< statistic for cross traffic
    void rateUpdate(double interval);   //!< use to update tx & Ebrc rate in real time
    void stop();                        //!< Stop the mbox function by setting the stop sign.
    void start();                       //!< Start the mbox 
    FairType GetFairness();             //!< get the fairness
    void SetWeight(vector<double> w);   //!< set the bw weight
    void SetRttRto(vector<double> rtt);    //!< set RTT and tRto for each flow
    vector<uint32_t> assignRandomLoss(vector<uint32_t> tax, vector<double> Ebrc, uint32_t N);       // !< tool to assign random loss
    vector<int> ExtractIndexFromTag(Ptr<const Packet> p);   //!< tool to extract index from tag, {index, cnt}, return -1 if not defined 
    void onMacTxDrop(Ptr<const Packet> p);          //!< redundant trace sink for mac tx drop
    void onPhyTxDrop(Ptr<const Packet> p);          //!< redundant trace sink for phy tx drop
    void onPhyRxDrop(Ptr<const Packet> p);          //!< redundant trace sink for phy rx drop
    void onAckRx(Ptr<const Packet> p);              //!< trace ack sink for senders
    void onMboxAck(Ptr<const Packet> p);            //!< track of mbox ack, essential for TCP loss update
    void onSenderTx(Ptr<const Packet> p);           //!< trace the sender tx, for TCP behaviour
    void txSink(Ptr<const Packet> p);               //!< for debug only
    void rxDrop(Ptr<const Packet> p);               //!< for debug only

    void TcPktInQ(uint32_t vOld, uint32_t vNew);    //!< record and write the value
    void DevPktInQ(uint32_t vOld, uint32_t vNew);
    void TcPktInRed(uint32_t vOld, uint32_t vNew);  // !< record pkt in link queue

    //!< all drop for debug
    void onRouterTx(Ptr<const Packet> p);           // for TX for rx router
    void onMacTxDrop2(Ptr<const Packet> p);
    void onPhyTxDrop2(Ptr<const Packet> p);
    void onPhyRxDrop2(Ptr<const Packet> p);


public:     // values ideally know, initialize() to set
    vector<uint32_t> rwnd;      // receive window of mbox
    vector<uint32_t> last_rwnd; // last receive window of mbox
    vector<uint32_t> lDrop;     // drop during window the link, basically RED
    vector<uint32_t> qDrop;     // queue drop for drop of UDP flow
    vector<uint32_t> nAck;      // ACK number received for UDP flow
    vector<uint32_t> last_lDrop;    // link drop in the last interval
    vector<uint32_t> totalRx;   // total # packet received from start
    vector<uint32_t> totalRxByte;    // total rx byte (to calculate data rate)
    vector<uint32_t> totalTxByte;    // total tx byte
    vector<uint32_t> totalDrop; // total # packet dropped by link from start
    vector<uint32_t> totalMDrop;
    uint32_t lastDrop;          // total last # drop packet (for slr)
    uint32_t lastRx;            // total last # rx packet
    vector<uint32_t> tcpwnd;    // sender's tcp window, for debug but very useful
    vector<int> ackwnd;    // mbox's ack wnd, essential in deciding the tcp loss
    map<uint32_t, uint32_t> addr2index;
    map<Ipv4Address, ProtocolType> ip2prot;
    vector<uint32_t> ltDrop;    // last total drop, for pl update
    vector<uint32_t> ltTx;      // last tx pkt sent, for pl update
    vector<double> mwnd;      // m_i for each flow, use to determine whether to turn off BE
    double scale;

    vector<uint32_t> bePkt;    // BE pkt number (now at the start of an interval)

private:    // values can/should be known locally inside mbox
    vector<uint32_t> cwnd;         
    vector<uint32_t> mDrop;     // drop window due to mbox, basically setEarlyDrop()
    double slr;                 // short-term loss rate, total
    vector<double> llr;         // long-term loss rate
    vector<double> dRate;       // rx data rate in kbps
    vector<double> txRate;
    vector<double> EbrcRate;    // EBRC rate for each flow
    vector<double> wEbrcRate;   // weighted EBRC rate
    vector<uint32_t> lastRx2;   // for rate calculation only
    vector<uint32_t> lastTx;    // for tx rate calculation only
    vector<double> lastArrival; // for packet level rate update
    vector<double> RTT;         // RTT for all traffic flow
    vector<double> tRto;        // delay of retransmision timeout

    vector<double> sm_rwnd;   // smoothed rwnd
    vector<double> sm_cwnd;   // smoothed cwnd
    double sm_capacity; // smoothed capacity
    
    uint32_t nSender;
    uint32_t nClient;       // initial # sender with normal traffic, might change (2nd stage work)
    uint32_t nAttacker;     // initial # sender with larger traffic
    uint32_t nReceiver;
    uint32_t slrWnd;        // # packet interval of slr update
    bool isEarlyDrop;
    bool isEbrc;            // if EBRC mode
    bool isTax;             // if tax mode
    double tStop;           // the stop time
    double beta;            // parameter for update llr
    double lrTh;            // loss rate threshold
    double slrTh;
    double llrTh;
    vector<bool> isCA;              // is congestion-avoidance

    AckAnalysis Acka;
    BeSoftControl Bsc;
    BandwidthManager Bm;
    SlowstartManager Ssm;
    LongRunSmoothManager Lrm;

    // for cross traffic
    uint32_t nCross;        // # cross traffic that won't be controlled
    vector<uint32_t> totalCrossByte;
    vector<uint32_t> lastCross;


    // only for test
    vector<uint32_t> txwnd;     // tx window of the sender router
    vector<uint32_t> txDwnd;    // mac tx drop window
    vector<uint32_t> phyRxDwnd; // phy rx drop window
    vector<uint32_t> phyTxDwnd; // phy tx drop window
    vector<uint32_t> rxwnd;     // rx window of the receiver router
    vector<vector<uint32_t>> dropWnd;   // all drop wnd for debug

    // internal parameters
    int MID;                            // mbox ID
    EventId fEID;               // event IDs of flow control and statistics
    EventId sEID;
    EventId cEID;
    Ptr<PointToPointNetDevice> device;  // where mbox is installed
    vector< Ptr<PointToPointNetDevice> > macRxDev;     // to set early drop before queue drop
    vector< vector<string> > fnames;    // for statistics
    vector< string> singleNames;
    vector< vector<ofstream> > fout;    // for statistics: output data to file
    vector< ofstream > singleFout;      // data irrelated to sender, e.g. slr, queue size
    double normSize;                    // normalized in kbit
    double pktSize;                     // ip pkt size in kbit
    double alpha;                       // parameter for moving average of tx rate and EBRC rate
    vector<double> pl0;
    ProtocolType protocol;
    FairType fairness;
    bool isStop;                        // stop flow control and statistics or not
    bool isStatReady;                   // ready to start statistics
    bool isTrackPkt;                    // switch to track the packet
    bool bypassMacRx;                   // if use MacTx to temporarily bypass MacRx
    bool is_monitor;                    // if enter monitor mode
    vector<double> weight;              // bw weight of flows, used in Priority fairness mode
    vector<uint32_t> tax;               // drop imposed on best-effort packets
    vector<uint32_t> lastTax;           // tax in last interval
    vector<double> sswnd;                     // use to check if a flow is legal and control illegal flow
    vector<bool> ssDrop;                // drop for latest slow start drop

    vector<uint32_t> congWnd;
    vector<double> rtt;
    vector<uint32_t> u_cnt;             // # intervals since last drop of each flow

    double a = 0.2;                     // a for smoothing
    double b = 1.2;                       // b for drop condition
    uint32_t safe_count = 0;            // # intervals since last link drop
    uint32_t safe_Th = 130;              // previously 50
    double explStep = 50;               // use for UDP exploration in the BSC BE Num calculation
    uint32_t counter = 1;               // for periodical long run reuse update
    uint32_t lr_period = 8;           // period in unit of intervals, as M in LRM module

    double rho;                         // update for 1 e_cwnd update
    map<uint32_t, Ipv4Address> index2des;   // flow index to destination IP, for set token rate
    SequenceNumber32 rxAckNo = SequenceNumber32(0);
    uint32_t curIndex;

};

}

#endif