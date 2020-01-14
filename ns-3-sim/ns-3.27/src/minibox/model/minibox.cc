/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "minibox.h"
#include "tools.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MiniBox");


/* ------------- Begin: implementation of AckAnalysis, i.e. record link/mbox drop by ACK ------------- */

AckAnalysis::AckAnalysis(uint32_t nSender)
{
    this->nSender = nSender;
    startNo = vector<uint32_t> (nSender, 0);
    lastNo = vector<uint32_t> (nSender, 0);
    times = vector<uint32_t> (nSender, 0);
    for(uint32_t i = 0; i < nSender; i ++)
    {
        mDropNo.push_back(vector<uint32_t> (1));
        seqNo.push_back(vector<uint32_t> (1));
    }
}

bool AckAnalysis::insert(Ptr<Packet> p, uint32_t index)
{
    NS_LOG_FUNCTION_NOARGS ();
    uint32_t addr = getIpSrcAddr(p).Get();
    if(addr2index.find(addr) != addr2index.end() && addr2index[addr] == index)
        return false;
    addr2index[addr] = index;
    return true;
}

int AckAnalysis::extract_index(Ptr<Packet> p)
{
    NS_LOG_FUNCTION_NOARGS ();
    uint32_t addr = getIpDesAddr(p).Get();
    if(addr2index.find(addr) == addr2index.end())
        return -1;
    return addr2index[addr];
}

void AckAnalysis::insert_pkt(uint32_t i, uint32_t No)
{
    seqNo[i].push_back(No);
    stringstream ss;
    ss << "   -- insert pkt of flow " << i << ": seq = " << No;
    // NS_LOG_INFO(ss.str());
}

void AckAnalysis::push_back(uint32_t i, uint32_t No)
{
    mDropNo[i].push_back(No);
    stringstream ss;
    ss << "     - mdrop of flow " << i << ". " << No;
    // NS_LOG_INFO(ss.str());
}

bool AckAnalysis::update(uint32_t i, uint32_t No)       // outside logic: if(update(i, No)) lDrop[i] ++;
{
    NS_LOG_FUNCTION ("Acka" << i << lastNo[i]);
    if(No > lastNo[i]) 
    {
        lastNo[i] = No;
        times[i] = 1;
        // if(mDropNo[i].size() > 50) clear(i, No);
    }
    else if (No == lastNo[i]) times[i] ++;

    if (times[i] > 50) NS_LOG_FUNCTION("Duplicate ACKs more than 50: flow " << i << "!");

    if(times[i] >= 3 && find(mDropNo[i].begin(), mDropNo[i].end(), No) == mDropNo[i].end())
    {
        mDropNo[i].push_back(No);       // ignore later dup ack
        return true;
    }
    return false;
}

bool AckAnalysis::update_udp(uint32_t i, uint32_t No)   // opposite to the TCP case above
{
    auto p = find(seqNo[i].begin(), seqNo[i].end(), No);
    bool res = p != seqNo[i].end();
    int pos = p - seqNo[i].begin();
    if(!res) 
    {
        cout << "  -- " << i << ". " << No << " Not found in seq No. table!" << endl;
    }
    else 
    {
        seqNo[i].erase(seqNo[i].begin() + pos);
    }
    return res;    // exist in seqNo table
}

uint32_t AckAnalysis::update_udp_drop(uint32_t i, uint32_t No)
{
    uint32_t res = 0;
    stringstream ss;
    ss << "   - udp link drop of flow " << i << " (" << lastNo[i] << "~" << No << "): ";
    for(uint32_t n = lastNo[i] + 1; n < No; n ++)
    {
        if(find(mDropNo[i].begin(), mDropNo[i].end(), n) == mDropNo[i].end())
        {
            ss << n << " ";
            res ++;
        }
    }
    // if (res > 0) NS_LOG_INFO(ss.str());
    if(No > lastNo[i])
        lastNo[i] = No;
    return res;
}

uint32_t AckAnalysis::count_mdrop(uint32_t i)
{
    uint32_t j = 0, cnt = 0;
    // cout << " -- mdrop size: " << mDropNo[i].size() <<" (last No = " << lastNo[i] << "): ";
    // for (uint32_t k = 0; k < mDropNo[i].size(); k ++)
    //     cout << mDropNo[i][k] << " ";
    // cout << endl;
    while(mDropNo[i][j] <= lastNo[i])
    {
        // if(mDropNo[i][j ++] <= startNo[i]) continue;
        // cnt ++;
        // if(j >= mDropNo[i].size()) break;

        if(mDropNo[i][j] <= startNo[i])
        {
            j ++;
            if(j >= mDropNo[i].size()) break;
            continue;
        }
        else
        {
            cnt ++;
            j ++;
            if(j >= mDropNo[i].size()) break;
        }
    }
    cout << " -- flow " << i << ": start: " << startNo[i] << " ; last: " << lastNo[i] << " ; cnt: " << cnt << endl;
    startNo[i] = lastNo[i];
    mDropNo[i].erase(mDropNo[i].begin(), mDropNo[i].begin() + j - 1);
    return cnt;
}

bool AckAnalysis::clear(uint32_t i, uint32_t No)
{
    bool res = false;
    for(int j = 0; j < mDropNo[i].size(); j ++)
        if(mDropNo[i][j] < No) 
        {
            mDropNo[i].erase(mDropNo[i].begin() + j);
            j = -1;
            if(!res) res = true;
        }
    return res;
}

void AckAnalysis::clear_seq()
{
    for(int i = 0; i < seqNo.size(); i ++)
    {
        seqNo[i].clear();
        seqNo[i].push_back(1);
    } 
}

uint32_t AckAnalysis::get_lastNo (uint32_t i)
{
    return lastNo.at(i);
}

uint32_t AckAnalysis::get_times (uint32_t i)
{
    return times.at(i);
}

vector<uint32_t> AckAnalysis::get_mDropNo (uint32_t i)
{
    return mDropNo.at(i);
}

map<uint32_t, uint32_t> AckAnalysis::get_map ()
{
    return addr2index;
}

/* ---------------- Begin: implementation of Flow  ---------------- */

Flow::Flow (Ptr<Node> txLeaf, Ptr<Node> rxLeaf, Ipv4AddressHelper leftAdd, Ipv4AddressHelper rightAdd, uint32_t rate, \
    vector<double> times, uint32_t maxBytes, uint16_t port, string typeId): \
    leftAddr(leftAdd), rightAddr(rightAdd), port(port), typeId(typeId), rate(rate), times(times), maxBytes(maxBytes)
{
    leafNode.Add (txLeaf);
    leafNode.Add (rxLeaf);
}

NetDeviceContainer Flow::build (string rate, string dsRate)       // create node, add link, install stack, assign address
{
    dsRate = dsRate.length()? dsRate : rate;
    CsmaHelper csma, dsCsma;
    csma.SetChannelAttribute ("DataRate", StringValue (rate));
    dsCsma.SetChannelAttribute ("DataRate", StringValue (dsRate));
    PointToPointHelper p2p, dsP2p;
    p2p.SetDeviceAttribute ("DataRate", StringValue (rate));
    p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));
    dsP2p.SetDeviceAttribute ("DataRate", StringValue (dsRate));
    dsP2p.SetChannelAttribute ("Delay", StringValue ("2ms"));

    InternetStackHelper stack;
    endNode.Create (2);

    // tx side
    NodeContainer client (endNode.Get (0), leafNode.Get (0));
    // NetDeviceContainer clientDev = csma.Install (client);
    NetDeviceContainer clientDev = p2p.Install (client);        // needed for simulation, emu will fail
    device.Add (clientDev);
    stack.Install (endNode.Get (0));                // ensure the device order
    interface.Add (leftAddr.Assign (clientDev));
    leftAddr.NewNetwork ();

    // rx side
    NodeContainer server (leafNode.Get (1), endNode.Get (1));
    // NetDeviceContainer serverDev = dsCsma.Install (server);
    NetDeviceContainer serverDev = dsP2p.Install (server);
    device.Add (serverDev);
    stack.Install (endNode.Get (1));
    interface.Add (rightAddr.Assign (serverDev));
    rightAddr.NewNetwork ();

    // logging
    stringstream ss;
    ss << "\n-------------- Flow Info (" << rate << ") --------------" << endl;
    ss << interface.GetAddress (0) << " -> " << interface.GetAddress (1) << " -----> " << \
    interface.GetAddress (2) << " -> " << interface.GetAddress (3) << endl;

    NS_LOG_INFO (ss.str());

    return device;
}

void Flow::setOnoff ()
{
    setSink ();

    OnOffHelper src (typeId, InetSocketAddress (interface.GetAddress (3), port));
    src.SetConstantRate (DataRate(rate));       // onTime = 1000, offTime = 0
    ApplicationContainer srcApp = src.Install (endNode.Get (0));

    srcApp.Start (Seconds (times[0]));
    srcApp.Stop (Seconds (times[1]));
    if (times.size() > 2)
    {
        srcApp.Start (Seconds (times[2]));
        srcApp.Stop (Seconds (times[3]));
    }
    NS_LOG_INFO ("Onoff set: des: " << interface.GetAddress (3) << ", port: " << port << \
        "; rate: " << (double)rate / 1000 << " kbps\n" );
}

void Flow::setBulk ()                  // install bulk application and set start/stop
{
    setSink ();

    BulkSendHelper src (typeId, InetSocketAddress (interface.GetAddress (3), port));
    src.SetAttribute ("MaxBytes", UintegerValue (maxBytes));
    ApplicationContainer srcApp = src.Install (endNode.Get (0));
    srcApp.Start (Seconds (times[0]));
    srcApp.Stop (Seconds (times[1]));
    if (times.size() > 2)
    {
        srcApp.Start (Seconds (times[2]));
        srcApp.Stop (Seconds (times[3]));
    }
    NS_LOG_INFO ("Bulk set: des: " << interface.GetAddress (3) << ", port: " << port << \
        "; bytes: " << maxBytes << endl);
}

void Flow::setPpbp (uint32_t nArrival, double duration)
{
    // PPBP flow for cross traffic: packet rate from the total rate given
    double factor = 3.0;
    uint32_t burstRate = rate / ((double)nArrival * duration * factor);
    string nArrivalStr = "ns3::ConstantRandomVariable[Constant=" + to_string (nArrival) + "]";
    string durationStr = "ns3::ConstantRandomVariable[Constant=" + to_string (duration) + "]";
    
    setSink ();

    PPBPHelper ph (typeId, InetSocketAddress (interface.GetAddress (3), port));
    ph.SetAttribute ("BurstIntensity", DataRateValue (DataRate (burstRate)));
    ph.SetAttribute ("MeanBurstArrivals", StringValue (nArrivalStr));
    ph.SetAttribute ("MeanBurstTimeLength", StringValue (durationStr));
    ApplicationContainer phApp = ph.Install (endNode.Get (0));
    phApp.Start (Seconds (times[0]));
    phApp.Stop (Seconds (times[1]));
    if (times.size() > 2)
    {
        phApp.Start (Seconds (times[2]));
        phApp.Stop (Seconds (times[3]));
    }
    NS_LOG_INFO ("PPBP set: des: " << interface.GetAddress (3) << ", port: " << port << "; burst rate: " \
        << burstRate / 1000 << " kbps, # arrival: " << nArrival << ", duration: " << duration << endl);
    NS_LOG_INFO (nArrivalStr << "\n" << durationStr);
}


void Flow::setTapBridge (string tap_left, string tap_right)
{
    TapBridgeHelper tbh;
    tbh.SetAttribute ("Mode", StringValue ("UseBridge"));
    tbh.SetAttribute ("DeviceName", StringValue (tap_left));
    tbh.Install (endNode.Get (0), device.Get (0));

    tbh.SetAttribute ("DeviceName", StringValue (tap_right));
    tbh.Install (endNode.Get (1), device.Get (3));
    NS_LOG_INFO ("Tap bridge set: " << tap_left << " : " << interface.GetAddress (0) << \
        ", " << tap_right << " : " << interface.GetAddress (3) << endl);
}

void Flow::setSink ()
{
    PacketSinkHelper sink (typeId, InetSocketAddress (Ipv4Address::GetAny (), port));
    ApplicationContainer sinkApp = sink.Install (endNode.Get (1));
    sinkApp.Start (Seconds (times[0]));
    sinkApp.Stop (Seconds (times[1]));
    NS_LOG_INFO ("Sink set on: " << interface.GetAddress (3) << ", time: " << \
    times[0] << " ~ " << times[1] << "s");
}

Ptr<Node> Flow::getHost (uint32_t i)
{
    NS_ASSERT_MSG ( i <= 1, "Host index can only be 0 or 1!");
    return endNode.Get (i); 
}

Ptr<NetDevice> Flow::getEndDevice (uint32_t i)
{
    NS_ASSERT_MSG (i <= 3, "End host only has 4 devices!");
    return device.Get (i);
}
    

/* ---------------- Begin: implementation of MiniBox  ---------------- */

MiniBox::MiniBox (vector<uint32_t> id, double period, double a, double b): \
    m_id(id), m_period(period), m_a(a), m_b(b)
{
    NS_LOG_FUNCTION (this << id[0] << id[1] << period);
    m_acka = AckAnalysis (1);

    // set up output stream
    string folder = "MboxStatistics", fname;
    vector<string> name = {"RttLlr", "AckLatency", "Rtt", "Llr"};
    for (uint32_t i = 0; i < name.size (); i ++)
    {
        fname = folder + "/" + name[i] + "_" + to_string(id[0]) + "_" + to_string(id[1]) + ".dat";
        remove (fname.c_str ());
        m_fout.push_back (ofstream (fname, ios::app | ios::out) );
    }
}

MiniBox::~MiniBox ()
{
    NS_LOG_FUNCTION (this);
    for (uint32_t i = 0; i < m_fout.size (); i ++)
        m_fout[i].close ();
}

void MiniBox::install (Ptr<Node> node, Ptr<NetDevice> device, HeaderType htype)
{
    NS_LOG_FUNCTION (this << node->GetId ());
    
    m_node = node;
    m_device = device;
    // m_socket = socket;      // assume each node only transmits 1 flow
    string nodeId = to_string (m_node->GetId ());
    m_sktPath = "/NodeList/" + nodeId + "/$ns3::TcpL4Protocol/SocketList/0/";
    m_hType = htype;

    NS_LOG_INFO ("- Path: " << m_sktPath);
}

void MiniBox::start (Time t)
{
    NS_LOG_FUNCTION (t);
    NS_LOG_INFO ("- Start at: " << t.GetSeconds () << " s");
    
    m_isRunning = true;
    Simulator::Cancel (m_startEvent);
    m_startEvent = Simulator::Schedule (t, &MiniBox::connect, this);
}

void MiniBox::connect ()
{
    Simulator::Schedule (Seconds(0), &MiniBox::update, this);

    // trace connect to all the sinks
    m_device->TraceConnectWithoutContext ("MacTx", MakeCallback (&MiniBox::onMacTx, this));
    m_device->TraceConnectWithoutContext ("MacRx", MakeCallback (&MiniBox::onMacRx, this));
    Config::Connect (m_sktPath + "RTT", MakeCallback(&MiniBox::onRttChange, this));
    Config::Connect (m_sktPath + "RxAck", MakeCallback(&MiniBox::onRxAck, this));
    Config::Connect (m_sktPath + "Latency", MakeCallback(&MiniBox::onLatency, this));

    // for debug only
    Config::Connect (m_sktPath + "CongestionWindow", MakeCallback(&MiniBox::onCwnd, this));
}

void MiniBox::stop (Time t)
{
    NS_LOG_FUNCTION (t);
    Simulator::Schedule (t, &MiniBox::disconnect, this);
}

void MiniBox::disconnect ()
{
    NS_LOG_FUNCTION_NOARGS ();
    NS_LOG_INFO ("- Stop and disconnect, run id:" << m_id[0] << ". " << m_id[1]);

    m_isRunning = false;
    m_device->TraceDisconnectWithoutContext ("MacTx", MakeCallback (&MiniBox::onMacTx, this));
    m_device->TraceDisconnectWithoutContext ("MacRx", MakeCallback (&MiniBox::onMacRx, this));
    Config::Disconnect (m_sktPath + "RTT", MakeCallback(&MiniBox::onRttChange, this));
    Config::Disconnect (m_sktPath + "RxAck", MakeCallback(&MiniBox::onRxAck, this));
    Config::Disconnect (m_sktPath + "Latency", MakeCallback(&MiniBox::onLatency, this));
}

void MiniBox::onMacTx (Ptr<const Packet> p)
{
    NS_LOG_FUNCTION (m_id[1] << m_rwnd);
    m_rwnd ++;
    m_acka.insert (p->Copy (), 0);        // index fixed to be 0 for each flow
}

void MiniBox::onMacRx (Ptr<const Packet> p)
{
    NS_LOG_FUNCTION (m_id[1] << m_drop);
    int index = m_acka.extract_index (p->Copy ());

    int ack;
    if (m_hType == PPP) ack = getTcpAckNo (p->Copy ());
    else ack = getTcpAckNoEth (p->Copy ());

    if (index < 0) 
    {
        NS_LOG_INFO ("index < 0: " << ack);
        return;
    }
    else if (m_acka.update (index, ack)) m_drop ++;
}

void MiniBox::onRttChange (string context, Time oldRtt, Time newRtt)
{
    NS_LOG_FUNCTION (this << newRtt.GetSeconds ());
    double nrtt = newRtt.GetSeconds ();
    if (m_rtt == 0) m_rtt = nrtt;
    else m_rtt = nrtt * m_a + m_rtt * (1 - m_a);
    m_fout[2] << Simulator::Now ().GetSeconds () << " " << nrtt << endl;
}

void MiniBox::onRxAck (string context, SequenceNumber32 vOld, SequenceNumber32 vNew)
{
    NS_LOG_FUNCTION (this << vNew.GetValue ());
    m_ackNo = vNew;
}

void MiniBox::onLatency (string context, Time oldLat, Time newLat)
{
    NS_LOG_FUNCTION (this << newLat.GetSeconds ());
    m_fout[1] << Simulator::Now ().GetSeconds () << " " << m_ackNo.GetValue () << \
        " " << newLat.GetSeconds () << endl;
}

void MiniBox::update ()
{
    NS_LOG_FUNCTION (m_id[1] << m_rwnd << m_drop);
    if (m_isRunning)
        Simulator::Schedule (Seconds (m_period), &MiniBox::update, this);

    // compute LLR, i.e. EMA of loss rate with tiny factor b
    double clr = m_rwnd > 0? (double) m_drop / m_rwnd : clr;          // current loss-rate
    if (m_rwnd < 5) m_llr = (1 - m_b) * m_llr;
    else m_llr = clr * m_b + m_llr * (1 - m_b);

    // record RTT and LLR
    NS_LOG_INFO (" - " << Simulator::Now ().GetSeconds () << "s flow " << m_id[1] << ". rwnd: " << m_rwnd \
        << ", drop: " << m_drop << ", rtt: " << m_rtt << ", llr: " << m_llr);
    m_fout[0] << Simulator::Now ().GetSeconds () << " " << m_rtt << " " << m_llr << endl;
    m_fout[3] << Simulator::Now ().GetSeconds () << " " << m_llr << endl;

    // clear windows
    m_rwnd = 0;
    m_drop = 0;
}

void MiniBox::onCwnd (string context, uint32_t oldCwnd, uint32_t newCwnd)
{
    NS_LOG_FUNCTION (" - flow " << m_id[1] << ". new cwnd: " << newCwnd);
}


}

