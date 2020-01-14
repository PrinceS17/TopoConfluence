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
 */

#include <type_traits>
#include "ns3/mrun.h"

using namespace std;
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("RunningModule");

typedef multimap<uint32_t, uint32_t>::iterator mmap_iter;
double controlInterval = 0.02;
double rateUpInterval = 0.002;

// for debug
fstream tokenOut;

void FirstBucketTrace (uint32_t oldV, uint32_t newV)
{
    double time = Simulator::Now().GetSeconds();
    tokenOut << time << " " << newV << endl;
    cout << time << ": first bucket " << newV << endl;
}

void SecondBucketTrace (uint32_t oldV, uint32_t newV)
{
    double time = Simulator::Now().GetSeconds();
    tokenOut << time << " " << newV << endl;
}



/* ------------- Begin: implementation of CapabilityHelper ------------- */
CapabilityHelper::CapabilityHelper(uint32_t flow_id, Ptr<Node> node, Ptr<NetDevice> device, Address addr)
{
    install(flow_id, node, device, addr);
}

vector<int> CapabilityHelper::GetNumFromTag(Ptr<const Packet> p)
{
    Ptr<const Packet> pcp = p->Copy();
    MyTag tag;
    if (!pcp->PeekPacketTag(tag)) return {-1, -1};
    MyApp temp;
    uint32_t tagScale = temp.tagScale;
    int index = tag.GetSimpleValue() / tagScale - 1;        // tag should include cnt, even not in debug mode
    int cnt = tag.GetSimpleValue() % tagScale;
    return {index, cnt};
}

void CapabilityHelper::SendAck(Ptr<const Packet> p)
{
    vector<int> vec = GetNumFromTag(p);
    if(vec[1] < 0) 
    { 
        // cout << "   - CapabilityHelper:: ACK not sent! " << vec[0] << ". " << vec[1] << endl; 
        return;
    }
    uint32_t ackNo = vec[1];
    curAckNo = ackNo;
    ackApp->SendAck(ackNo);
    // cout << "   - CapabilityHelper:: ACK " << ackNo << " of flow " << flow_id << " sent!" << endl;
}

void CapabilityHelper::install(uint32_t flow_id, Ptr<Node> node, Ptr<NetDevice> device, Address addr)
{
    this->flow_id = flow_id;
    curAckNo = 9999;
    Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice> (device);
    this->device = p2pDev;

    // set socket
    TypeId tpid = UdpSocketFactory::GetTypeId();        // currently only for UDP
    Ptr<Socket> skt = Socket::CreateSocket(node, tpid);

    // set application
    ackApp = CreateObject<MyApp> ();
    ackApp->isTrackPkt = true;            // seems doesn't matter
    ackApp->SetTagValue(flow_id + 1);      // begin from 1
    ackApp->Setup(skt, addr, 15, DataRate('1Mbps'));   // pkt size and rate aren't necessary
    // node->AddApplication(ackApp);
    ackApp->StartAck();

    cout << "   - CapabilityHelper is installed on RX node " << flow_id << ": tag scale = " << ackApp->tagScale << endl;
}

uint32_t CapabilityHelper::getFlowId()
{
    return flow_id;
}

vector<uint32_t> CapabilityHelper::getCurAck()
{
    return {flow_id, curAckNo};
}

/* ------------- Begin: implementation of RunningModule ------------- */

uint32_t RunningModule::SetNode(uint32_t i, uint32_t id)
{
    Group g = groups.at(i);
    if (id2nodeIndex.find(id) != id2nodeIndex.end())
        return id2nodeIndex[id];
    nodes.Create(1);
    id2nodeIndex[id] = nodes.GetN() - 1;
    return nodes.GetN() - 1;
}

uint32_t RunningModule::SetRouter(Ptr<Node> pt, uint32_t i, uint32_t id)
{
    Group g = groups[i];
    nodes.Add(pt);
    id2nodeIndex[id] = nodes.GetN() - 1;
    return nodes.GetN() - 1;
}

uint32_t RunningModule::SetNode(uint32_t type, uint32_t i, uint32_t n)
{
    Group g = groups.at(i);
    uint32_t id;
    switch(type)
    {
    case 0:
        id = g.txId[n];
        break;
    case 1:
        id = g.rxId[n];
        break;
    case 2:
        id = g.routerId[n];
        break;
    default: ;
    }
    return SetNode(i, id);
}

Ptr<Node> RunningModule::GetNode(uint32_t i, uint32_t id)
{
    // use the mapping from node id to index in NodeContainer
    if(id2nodeIndex.find(id) == id2nodeIndex.end())
        return 0;
    return nodes.Get(id2nodeIndex[id]);
}

Ipv4Address RunningModule::GetIpv4Addr(uint32_t i, uint32_t id, uint32_t k)
{
    pair<uint32_t, uint32_t> pr = make_pair(id, k);
    if(id2ipv4Index.find(pr) == id2ipv4Index.end())
        return 0;

    return ifc.GetAddress(id2ipv4Index[pr]);
}

uint32_t RunningModule::GetId()
{   return ID; }

RunningModule::RunningModule(vector<double> t, vector<Group> grp, ProtocolType pt, vector<string> bnBw, vector<string> bnDelay, vector<string> delay, double dsBw, vector<bool> fls, uint32_t size)
{
    // constant setting
    ID = rand() % 10000;
    nSender = 0;
    nReceiver = 0;
    groups = grp;
    for(Group g:groups)
    {
        nSender += g.txId.size();
        nReceiver += g.rxId.size();
    }
    pktSize = size;
    rtStart = t.at(0);
    rtStop = t.at(1);

    for(uint32_t i = 0; i < nSender; i ++)
    {
        txStart.push_back(t.at(i + 2));
        txEnd.push_back(t.at(i + nSender + 2));
    }

    protocol = pt;
    bottleneckBw = bnBw;
    bottleneckDelay = bnDelay;
    this->delay = delay;
    downstreamBw = dsBw;
    isTrackPkt = fls.at(0);
    bypassMacRx = fls.at(1);

    // routers.Create(3);  // left, right, mbox (link order: mbox-> left-> right)
}

RunningModule::~RunningModule(){}

void RunningModule::buildTopology(vector<Group> grp)
{
// set up the random variables for downstream bandwidth and delay (downstreamBw is deprecated)
    Ptr<NormalRandomVariable> bwRV = CreateObject<NormalRandomVariable> ();
    Ptr<NormalRandomVariable> delayRV = CreateObject<NormalRandomVariable> ();
    bwRV->SetAttribute ("Mean", DoubleValue (downstreamBw));			// 50 Mbps: downstream bottleneck; 1Gbps: co-bottleneck
    bwRV->SetAttribute ("Variance", DoubleValue (25));
    delayRV->SetAttribute ("Mean", DoubleValue (3));
    delayRV->SetAttribute ("Variance", DoubleValue (1));

    PointToPointHelper leaf, downstream, cross;
    leaf.SetDeviceAttribute("DataRate", StringValue(normalBw));		// upstream settings
    leaf.SetChannelAttribute("Delay", StringValue(delay[0]));
//    downstream.SetDeviceAttribute ("DataRate", StringValue(downstreamBw));
//    downstream.SetChannelAttribute ("Delay", StringValue (delay[0]));
    cross.SetDeviceAttribute("DataRate", StringValue(normalBw));
    cross.SetChannelAttribute("Delay", StringValue(delay[1]));
    
    NodeContainer rt_ptr;
    NodeContainer leaf_ptr;
    stringstream ss;
    InternetStackHelper stack;

    for(uint32_t i = 0; i < grp.size(); i ++)
    {
        Group g = grp[i];
        bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBw[i]));
        bottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay[i]));
        bottleneck.SetDeviceAttribute("Mtu", StringValue(mtu));

        // add mbox node
        vector<uint32_t> rt_id(3);                      // mbox --> tx router --> rx router
        for(auto j:{0, 1, 2})                           // unique for mbox (UFM)
        {
            rt_id[j] = SetNode(2, i, j);

            rt_ptr.Add(nodes.Get(rt_id[j]));
        }
        Ptr<Node> mb_router = nodes.Get(rt_id[0]);
        Ptr<Node> tx_router = nodes.Get(rt_id[1]);
        Ptr<Node> rx_router = nodes.Get(rt_id[2]);
        routerDevice.Add( leaf.Install(mb_router, tx_router) );     // UFM
        routerDevice.Add( bottleneck.Install(tx_router, rx_router) );

        for(uint32_t j = 0; j < g.txId.size(); j ++)    // left leaf nodes --> mbox
        {
            uint32_t idx = SetNode(0, i, j);
            NetDeviceContainer ndc1;
            if(j < g.N_ctrl) ndc1 = leaf.Install(nodes.Get(idx), mb_router);
            else ndc1 = cross.Install(nodes.Get(idx), mb_router);
            txDevice.Add(ndc1.Get(0));
            txRouterDevice.Add(ndc1.Get(1));
            if(!i) leaf_ptr.Add(nodes.Get(idx));
        }
        NS_LOG_INFO ("Building downstream link ...");
        for(uint32_t j = 0; j < g.rxId.size(); j ++)    // rx router --> right leaf nodes
        {
            uint32_t idx = SetNode(1, i, j);
            NetDeviceContainer ndc2;

// set downstream BW attribute here
            string newRate = to_string(bwRV->GetValue ()) + "Mbps";
            string newDelay = to_string(delayRV->GetValue ()) + "ms";
            downstream.SetDeviceAttribute ("DataRate", StringValue (newRate));
            downstream.SetChannelAttribute ("Delay", StringValue (newDelay));
            NS_LOG_INFO ("Flow " << j << ": " << newRate << ", " << newDelay);

            if(j < g.N_ctrl) ndc2 = downstream.Install(rx_router, nodes.Get(idx));
            else ndc2 = cross.Install(rx_router, nodes.Get(idx));
            rxRouterDevice.Add(ndc2.Get(0));
            rxDevice.Add(ndc2.Get(1));
            if(!i) leaf_ptr.Add(nodes.Get(idx));
        }
    }
    stack.Install(nodes);

/*
        // tested: no mbox at first, only tx and rx router
        vector<uint32_t> rt_id(2);
        for(auto j:{0, 1})
        {
            rt_id[j] = SetNode(2, i, j);
            if(!i) rt_ptr.Add(nodes.Get(rt_id[j]));     // test
        }
        Ptr<Node> tx_router = nodes.Get(rt_id[0]);
        Ptr<Node> rx_router = nodes.Get(rt_id[1]);
        routerDevice.Add( bottleneck.Install(tx_router, rx_router) );
        
        for(uint32_t j = 0; j < g.txId.size(); j ++)    // left leaf nodes --> tx router
        {
            uint32_t idx = SetNode(0, i, j);
            NetDeviceContainer ndc1 = leaf.Install(nodes.Get(idx), tx_router);
            txDevice.Add(ndc1.Get(0));
            txRouterDevice.Add(ndc1.Get(1));
            if(!i && !j) leaf_ptr.Add(nodes.Get(idx));  // test
        }
        for(uint32_t j = 0; j < g.rxId.size(); j ++)    // rx router --> right leaf nodes
        {
            uint32_t idx = SetNode(1, i, j);
            NetDeviceContainer ndc2 = leaf.Install(rx_router, nodes.Get(idx));
            rxRouterDevice.Add(ndc2.Get(0));
            rxDevice.Add(ndc2.Get(1));
            if(!i && !j) leaf_ptr.Add(nodes.Get(idx));  // test
        }
        stack.Install(nodes);               // install the stack at last
    }
*/

    // verify the node id and state
    NodeContainer TmpNode;
    TmpNode.Add( GetNode(0, grp[0].txId[0]) );
    TmpNode.Add( GetNode(0, grp[0].txId[1]) );          // check it in double dumbbell topology
    TmpNode.Add( GetNode(0, grp[0].routerId[0]) );     // mbox router
    TmpNode.Add( GetNode(0, grp[0].routerId[1]) );     // tx router
    TmpNode.Add( GetNode(0, grp[0].routerId[2]) );     // rx router
    TmpNode.Add( GetNode(0, grp[0].rxId[0]) );
    TmpNode.Add( GetNode(0, grp[0].rxId[1]) );
  
    uint32_t Nt = groups[0].txId.size();

    // NS_ASSERT_MSG(TmpNode.Get(0) == leaf_ptr.Get(0), "Ptr<Node> for TX leaf (0,0) is incorrect!");
    // NS_ASSERT_MSG(TmpNode.Get(1) == leaf_ptr.Get(1), "Ptr<Node> for TX leaf (0,1) is incorrect!");
    
    // NS_ASSERT_MSG(TmpNode.Get(2) == rt_ptr.Get(0), "Ptr<Node> for mbox router is incorrect!");
    // NS_ASSERT_MSG(TmpNode.Get(3) == rt_ptr.Get(1), "Ptr<Node> for TX router is incorrect!");
    // NS_ASSERT_MSG(TmpNode.Get(4) == rt_ptr.Get(2), "Ptr<Node> for RX router is incorrect!");
    
    // NS_ASSERT_MSG(TmpNode.Get(5) == leaf_ptr.Get(Nt), "Ptr<Node> for RX leaf (0,0) is incorrect!");
    // NS_ASSERT_MSG(TmpNode.Get(6) == leaf_ptr.Get(Nt + 1), "Ptr<Node> for RX leaf (0,1) is incorrect!");

    vector<uint32_t> iter = {0,1,2,3,4,5,6};
    if(groups.size() > 1)
    {   
        iter = {0,1,2,3,4,5,6,7,8,9};
        TmpNode.Add( GetNode(1, grp[1].routerId[0]) );     // mbox router
        TmpNode.Add( GetNode(1, grp[1].routerId[1]) );     // tx router
        TmpNode.Add( GetNode(1, grp[1].routerId[2]) );     // rx router
    

        // NS_ASSERT_MSG(TmpNode.Get(7) == rt_ptr.Get(3), "Ptr<Node> for mbox router is incorrect!");
        // NS_ASSERT_MSG(TmpNode.Get(8) == rt_ptr.Get(4), "Ptr<Node> for TX router is incorrect!");
        // NS_ASSERT_MSG(TmpNode.Get(9) == rt_ptr.Get(5), "Ptr<Node> for RX router is incorrect!");    
    }
    // NS_LOG_INFO("Ptr<Node> check passed.");
    NS_LOG_INFO("Ptr<Node> check disabled.");

    // debug: address verification
    ss << "Node verification:  # dev    MAC addresses   MTU" << endl;
    for(auto i: iter)
    {
        ss << "                    " << TmpNode.Get(i)->GetNDevices() << "        ";
        for(uint32_t j = 0; j < TmpNode.Get(i)->GetNDevices(); j ++)
            ss << TmpNode.Get(i)->GetDevice(j)->GetAddress() << "   " << TmpNode.Get(i)->GetDevice(j)->GetMtu() << endl << "                        ";
        ss << endl;
    }
    NS_LOG_INFO(ss.str());

}

void RunningModule::configure(double stopTime, ProtocolType pt, vector<string> bw, string dsCrossRate, vector<double> onoffTime, vector<string> delay, vector<MiddlePoliceBox>& mboxes, vector<double> Th)
{
    NS_LOG_FUNCTION("Begin.");
    NS_LOG_INFO("Starting running module ... ");
    protocol = pt;
    bottleneckBw = bw;
    this->dsCrossRate = dsCrossRate;
    this->onoffTime = onoffTime;
    bottleneckDelay = delay;
    qc = setQueue(groups, bottleneckBw, bottleneckDelay, Th);
    ifc = setAddress();
    sinkApp = setSink(groups, protocol);

    if(pt == UDP)
    {
        setCapabilityHelper(groups);
        for(uint32_t i = 0; i < chelpers.size(); i ++)  // for debug only
        {
            cout << "    -- After installation: flow " << chelpers[i].getFlowId() << " has ptr " << &chelpers.at(i) << endl;
        }
    }

    senderApp = setSender(groups, protocol);
    dsCrossApp = setDsCross (groups);               // set up downstream cross traffic

    this->mboxes = mboxes;
    connectMboxCtrl(groups, controlInterval, 0.2, rateUpInterval);
    // connectMbox(groups, controlInterval, 0.2, rateUpInterval);        // not work either
    // connectMbox(groups, controlInterval, 1, rateUpInterval);
    start();
}

QueueDiscContainer RunningModule::setQueue(vector<Group> grp, vector<string> bnBw, vector<string> bnDelay, vector<double> Th)
{
    NS_LOG_FUNCTION("Begin.");

    // set RED queue and TBFQ
    // string token_rate = "50Mbps";
    // string peak_rate = "50Mbps";
    string token_rate = "10Gbps";
    string peak_rate = "10Gbps"; 
    QueueDiscContainer qc;
    uint32_t qMax = 100;
    for(uint32_t i = 0; i < grp.size(); i ++)
    {
        TrafficControlHelper fq_tch;
        // ------------- Commented for ns-3.27 -------------------------
        // fq_tch.SetRootQueueDisc ("ns3::FqTbfQueueDisc",
        //                          "MaxSize", QueueSizeValue (QueueSize ("1000p")),
        //                          "Burst", UintegerValue (100000),
        //                          "Rate", StringValue (token_rate),
        //                          "PeakRate", StringValue (peak_rate));

        fq_tch.SetRootQueueDisc("ns3::RedQueueDisc", 
                                 "MinTh", DoubleValue(5),
                                 "MaxTh", DoubleValue(15),
                                 "QueueLimit", UintegerValue(qMax),
                                 "LinkBandwidth", StringValue(token_rate),
                                 "LinkDelay", StringValue("1ms"));

        TrafficControlHelper red_tch;
        if(Th.empty()) red_tch.SetRootQueueDisc("ns3::RedQueueDisc", 
                                            "MinTh", DoubleValue(5),
                                            "MaxTh", DoubleValue(15),
                                            "QueueLimit", UintegerValue(qMax),
                                            "LinkBandwidth", StringValue(bnBw.at(i)),
                                            "LinkDelay", StringValue(bnDelay.at(i)));
        else red_tch.SetRootQueueDisc("ns3::RedQueueDisc",
                                "MinTh", DoubleValue(Th.at(0)),
                                "MaxTh", DoubleValue(Th.at(1)),
                                "QueueLimit", UintegerValue(qMax),
                                "LinkBandwidth", StringValue(bnBw.at(i)),
                                "LinkDelay", StringValue(bnDelay.at(i)));

        // qc.Add(tch.Install(GetNode(i, grp.at(i).routerId[0])->GetDevice(0)));
        // Ptr<NetDevice> mbRouter = GetNode(i, grp[i].routerId[0])->GetDevice(0);
        // Ptr<NetDevice> txRouter = GetNode(i, grp[i].routerId[1])->GetDevice(0);      // should word, but complicated
        Ptr<NetDevice> mbRouter = routerDevice.Get(4 * i);
        Ptr<NetDevice> txRouter = routerDevice.Get(4 * i + 2);      // router dev: 4 for each group

        qc.Add(fq_tch.Install(mbRouter));
        qc.Add(red_tch.Install(txRouter));
        cout << "type id of queue: " << qc.Get(0)->GetInstanceTypeId() << ", " << 
            qc.Get(1)->GetInstanceTypeId() << endl;
    }

    return qc;
}

Ipv4InterfaceContainer RunningModule::setAddress()
{
    // assign Ipv4 addresses: carefully record the index before, because we cannot push back one by one
    Ipv4AddressHelper ih("10.1.0.0", "255.255.255.0");
    NetDeviceContainer ndc;
    Ipv4InterfaceContainer ifc;
    uint32_t index = 0;
    map<uint32_t, string> type2name;        // for debug
    type2name[1] = "TX";
    type2name[2] = "RX";
    type2name[3] = "Router";
    stringstream ss;
    vector<uint32_t> pt = {0, 0, 0, 0, 0};     // router, tx, tx-rt, rx, rx-rt, 
    vector<string> ipbase;

    for(uint32_t i = 0; i < groups.size(); i ++)
    {
        Group g = groups[i];
    
        for (uint32_t j : {1,2,3})
            ipbase.push_back(to_string(10 * i + 11) + "." + to_string(j) + ".0.0");

        // mbox router and tx router
        NetDeviceContainer ndc0;
        ndc0.Add(routerDevice.Get(pt[0] ++));
        ndc0.Add(routerDevice.Get(pt[0] ++));
        ifc.Add( ih.Assign(ndc0) );
        id2ipv4Index[make_pair(g.routerId[0], 0)] = index ++;   // mbox MacTx
        id2ipv4Index[make_pair(g.routerId[1], 0)] = index ++;   // tx router MacRx
        
        ih.SetBase (ipbase[3 * i].c_str(), "255.255.255.0");

        // tx router and rx router
        NetDeviceContainer ndc1;
        ndc1.Add(routerDevice.Get(pt[0] ++));
        ndc1.Add(routerDevice.Get(pt[0] ++));
        ifc.Add( ih.Assign(ndc1) );
        id2ipv4Index[make_pair(g.routerId[1], 1)] = index ++;   // tx router MacTx (late created)
        id2ipv4Index[make_pair(g.routerId[2], 0)] = index ++;   // rx router MacRx
        ih.SetBase (ipbase[3 * i + 1].c_str(), "255.255.255.0");

        for(uint32_t j = 0; j < g.txId.size(); j ++)
        {
            // mbox and left router address
            NetDeviceContainer ndc2;
            ndc2.Add(txDevice.Get(pt[1] ++));
            ndc2.Add(txRouterDevice.Get(pt[2] ++));
            ifc.Add( ih.Assign(ndc2) );
            id2ipv4Index[make_pair(g.txId[j], 0)] = index ++;
            id2ipv4Index[make_pair(g.routerId[0], j + 1)] = index ++;   // note: mbox is leftmost
            ih.NewNetwork();
        }
        ih.SetBase (ipbase[3 * i + 2].c_str(), "255.255.255.0");
        for(uint32_t j = 0; j < g.rxId.size(); j ++)
        {
            // rx and right router address
            NetDeviceContainer ndc3;
            ndc3.Add(rxRouterDevice.Get(pt[4] ++));
            ndc3.Add(rxDevice.Get(pt[3] ++));
            ifc.Add( ih.Assign(ndc3) );
            id2ipv4Index[make_pair(g.routerId[2], j + 1)] = index ++;
            id2ipv4Index[make_pair(g.rxId[j], 0)] = index ++;
            ih.NewNetwork();
        }
    }

    // IP verification
    for(uint32_t i = 0; i < groups.size(); i ++)
    for(auto t:{1,2,3})
    {
        vector<uint32_t> ids = t == 1? groups[i].txId:
                               t == 2? groups[i].rxId: groups[i].routerId;
        ss << type2name[t] << ": " << endl;
        for(uint32_t j = 0; j < ids.size(); j ++)
        {
            for(uint32_t k = 0; k < GetNode(i, ids[j])->GetNDevices() - 1; k ++)
            {
                ss << "       ";
                ifc.GetAddress( id2ipv4Index[make_pair(ids[j], k)] ).Print(ss);
                ss << endl;
            }
            ss << "     ---------------     " << endl;
        }
    }

/*  // original address assignment
    for(uint32_t i = 0; i < groups.size(); i ++)
    for(auto t:{1, 2, 3})
    {
        vector<uint32_t> ids = t == 1? groups[i].txId:
                               t == 2? groups[i].rxId: groups[i].routerId;
        for(uint32_t j = 0; j < ids.size(); j ++)
        {
            Ptr<Node> node = GetNode(i, ids[j]);
            for(uint32_t k = 0; k < node->GetNDevices() - 1; k ++)  // how about discarding loop back device
                ndc.Add( node->GetDevice(k) );
        }
    }
    
    // assign Ipv4 addresses and set the index map
    ifc.Add(ih.Assign(ndc));
    for(uint32_t i = 0; i < groups.size(); i ++)
    for(auto t:{1,2,3})
    {
        vector<uint32_t> ids = t == 1? groups[i].txId:
                               t == 2? groups[i].rxId: groups[i].routerId;
        ss << type2name[t] << ": " << endl;
        for(uint32_t j = 0; j < ids.size(); j ++)
        for(uint32_t k = 0; k < GetNode(i, ids[j])->GetNDevices() - 1; k ++)
        {
            id2ipv4Index[make_pair(ids[j], k)] = index;
            ss << "       ";                        // for debug
            ifc.GetAddress(index).Print(ss); 
            ss << endl;
            index ++;
        }
    }
*/

    NS_LOG_INFO(ss.str());
    return ifc;
}

ApplicationContainer RunningModule::setSink(vector<Group> grp, ProtocolType pt)
{
    NS_LOG_FUNCTION("Begin.");
    // string ptStr = pt == TCP? "ns3::TcpSocketFactory":"ns3::UdpSocketFactory";
    for(uint32_t i = 0; i < grp.size(); i ++)
    {
        Group g = grp.at(i);
        for(uint32_t j = 0; j < g.txId.size(); j ++)
        {
            uint32_t tid = g.txId.at(j);
            string ptStr = g.tx2prot[tid] == TCP? "ns3::TcpSocketFactory":"ns3::UdpSocketFactory";
            uint32_t port = g.rate2port[ g.tx2rate[tid] ];
            Address sinkLocalAddr (InetSocketAddress(Ipv4Address::GetAny(), port));
            PacketSinkHelper psk(ptStr, sinkLocalAddr);

            // find RX corresponding to the txId: equal_range, need testing
            pair<mmap_iter, mmap_iter> res = g.tx2rx.equal_range(tid);
            stringstream ss;
            ss << "TX ID: " << tid << ";    RX ID: ";
            for(mmap_iter it = res.first; it != res.second; it ++)
            {
                Ptr<Node> pn = GetNode(i, it->second);
                sinkApp.Add(psk.Install(pn));   // it->second: rx ids
                ss << it->second << " . Addr: " << pn->GetDevice(0)->GetAddress();
            }
            NS_LOG_INFO(ss.str());
        }
    }
    return sinkApp;
}

vector< CapabilityHelper > RunningModule::setCapabilityHelper(vector<Group> grp)
{
    NS_LOG_FUNCTION("Begin.");
    int idx = 0;
    stringstream ss;
    for (uint32_t i = 0; i < grp.size(); i ++)
    {
        Group g = grp.at(i);
        uint32_t n = g.rxId.size();
        
        for (uint32_t j = 0; j < g.txId.size(); j ++)
        {
            uint32_t tId = g.txId[j];
            // uint32_t rx_id = g.tx2rx.equal_range(tId);
            uint32_t rx_id = g.rxId[j];                 // short cut, not flexible for the case that TX are not equal to RX!!!
            uint32_t ri = find(g.rxId.begin(), g.rxId.end(), rx_id) - g.rxId.begin();
            string rate = g.tx2rate.at(tId);
            uint32_t port = g.rate2port.at(rate);

            Ipv4Address addr = GetIpv4Addr(i, tId);
            Address sourceAddr(InetSocketAddress(addr, port));

            Ptr<Node> rx_node = GetNode(i, rx_id);
            Ptr<NetDevice> rx_device = rx_node->GetDevice(0);
            
            if (g.tx2prot[tId] == UDP)          // flow oriented
            {
                CapabilityHelper tmp;
                chelpers.push_back(tmp);
                chelpers.at(idx ++).install(ri, rx_node, rx_device, sourceAddr);
            }

            // by the way, set ipv4 -> protocol mapping
            ip2prot[addr] = g.tx2prot[tId];

        }
    }
    NS_LOG_INFO(ss.str());

    // set tracing in another part to avoid coupling
    idx = 0;
    for (uint32_t i = 0; i < grp.size(); i ++)
    {
        Group g = grp.at(i);
        for (uint32_t j = 0; j < g.txId.size(); j ++)
        {
            if (g.tx2prot[g.txId[j]] != UDP) continue;
            uint32_t rId = g.rxId[j];
            uint32_t ri = j;
            Ptr<Node> rx_node = GetNode(i, rId);
            Ptr<NetDevice> rx_device = rx_node->GetDevice(0);
            rx_device->TraceConnectWithoutContext("MacRx", MakeCallback(&CapabilityHelper::SendAck, &chelpers.at(idx)));
            cout << "   - f " << j << " 's addr in set chelper: " << &chelpers.at(idx) << endl;
            idx ++;
        }
    }
    return chelpers;
}

vector< Ptr<MyApp> > RunningModule::setSender(vector<Group> grp, ProtocolType pt)
{
    NS_LOG_FUNCTION("Begin.");
    vector<Ptr<MyApp>> appc;
    stringstream ss;
    for(uint32_t i = 0; i < grp.size(); i ++)
    {
        ss << "Set sender " << i << " ... " << endl;

        Group g = grp.at(i);
        for(auto tId:g.txId)
        {
            pair<mmap_iter, mmap_iter> res = g.tx2rx.equal_range(tId);
            for(mmap_iter it = res.first; it != res.second; it ++)
            {
                vector<uint32_t>::iterator it2 = find(g.txId.begin(), g.txId.end(), tId);
                // uint32_t tag = i*u + (uint32_t)(it2 - g.txId.begin()) + 1;      // tag: index in sender
                uint32_t tag = (uint32_t)(it2 - g.txId.begin()) + 1;
                cout << "RX id: " << it->second << "; Tag : " << tag << "; " << tId << " -> " << it->second << endl;
                appc.push_back(netFlow(i, tId, it->second, tag));
            }
        }
    }
    NS_LOG_INFO(ss.str());
    return appc;
}

Ptr<MyApp> RunningModule::netFlow(uint32_t i, uint32_t tId, uint32_t rId, uint32_t tag)
{
    // parse rate and port
    Group g = groups.at(i);
    string rate = g.tx2rate.at(tId);
    TypeId tpid = g.tx2prot[tId] == TCP? TcpSocketFactory::GetTypeId():UdpSocketFactory::GetTypeId();
    uint32_t port = g.rate2port.at(rate);
    
    uint32_t ri = find(g.rxId.begin(), g.rxId.end(), rId) - g.rxId.begin();
    uint32_t ti = find(g.txId.begin(), g.txId.end(), tId) - g.txId.begin();

    // set socket
    Ptr<Socket> skt = Socket::CreateSocket(GetNode(i, tId), tpid); 
    cout << "Socket type id: " << tpid << endl;

    // if(g.tx2prot[tId] == TCP) skt->SetAttribute ("Sack", BooleanValue (false));
    Address sinkAddr(InetSocketAddress(GetIpv4Addr(i, rId), port));
    
    Ptr<MyApp> app = CreateObject<MyApp> ();
    app->isTrackPkt = isTrackPkt;
    app->SetTagValue(tag);
    app->Setup(skt, sinkAddr, pktSize, DataRate(rate));
    GetNode(i, tId)->AddApplication(app);

    // logging
    stringstream ss;
    ss << "Group " << i << ": " << tId << "->" << rId << " ; port: " << port << endl;
    ss << GetNode(i, tId)->GetDevice(0)->GetAddress() << " -> " << GetNode(i, rId)->GetDevice(0)->GetAddress() << "; RX IP: ";
    GetIpv4Addr(i, rId).Print(ss);
    ss << " ; # dev: " << GetNode(i, tId)->GetNDevices();
    NS_LOG_INFO(ss.str());
    return app;
}

ApplicationContainer RunningModule::setDsCross(vector<Group> grp)
{
    NS_LOG_FUNCTION ("Set up downstream cross traffic!");
    PointToPointHelper p2p;
    p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));
    p2p.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
    InternetStackHelper stack;
    Ipv4AddressHelper addr;
    addr.SetBase ("20.0.0.0", "255.255.255.252");

    OnOffHelper oh("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 9));
    oh.SetAttribute ("DataRate", StringValue (dsCrossRate));
    oh.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=" + to_string(onoffTime[0]) + "]" ));
    oh.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=" + to_string(onoffTime[1]) + "]" ));
    
    ApplicationContainer apc;
    for(uint32_t i = 0; i < grp.size(); i ++)
    {
        Group g = grp[i];
        
        // uint32_t port = 5000;
        uint32_t rtId = g.routerId[1];
        for (uint32_t j = 0; j < g.txId.size(); j ++)
        {
            uint32_t rId = g.rxId[j];
            uint32_t tId = g.txId[j];
            uint32_t port = g.rate2port[g.tx2rate[tId]];

            // generate cross traffic generators for each rx
            NetDeviceContainer gdev1, gdev2;
            Ipv4InterfaceContainer ifc;
            nodes.Create(2);
            NodeContainer Gen (nodes.Get (nodes.GetN () - 2), nodes.Get (nodes.GetN () - 1));
            gdev1 = p2p.Install (Gen.Get (0), GetNode (i, g.routerId[1]));
            gdev2 = p2p.Install (Gen.Get (1), GetNode (i, g.routerId[1]));
            stack.Install (Gen);
            ifc.Add (addr.Assign (gdev1));
            addr.NewNetwork ();
            ifc.Add (addr.Assign (gdev2));
            addr.NewNetwork ();

            oh.SetAttribute ("Remote", AddressValue( InetSocketAddress (GetIpv4Addr (i, rId), port)) );
            apc.Add (oh.Install (Gen));
        
        }
    }

    stringstream ss;
    ss << "Downstream cross traffic set up: rate: " << dsCrossRate << ", on: " << onoffTime[0] << ", off: " << onoffTime[1];
    NS_LOG_INFO(ss.str());
    return apc;
}

void RunningModule::connectMbox(vector<Group> grp, double interval, double logInterval, double ruInterval)
{
    NS_LOG_FUNCTION("Connect Mbox ... ");
    for(uint32_t i = 0; i < grp.size(); i ++)
    {
        // MiddlePoliceBox& mb = mboxes.at(i);         // not sure
        Ptr<NetDevice> txRouter = GetNode(i, grp[i].routerId[0])->GetDevice(0);     // 3 router scenario: txRouter is the 1st one
        Ptr<NetDevice> rxRouter = GetNode(i, grp[i].routerId[2])->GetDevice(0);
        Ptr<Node> txNode = GetNode(i, grp[i].routerId[0]);
        Ptr<Node> rxNode = GetNode(i, grp[i].routerId[2]);
        NetDeviceContainer nc;
        for(uint32_t j = 1; j <= grp[i].txId.size(); j ++)
            nc.Add(txNode->GetDevice(j));               // add rx side devices, this line will add the loop back

        // install mbox
        if(bypassMacRx) mboxes.at(i).install(txRouter);     // install only the router net dev for bottleneck link
        else mboxes.at(i).install(nc);                      // install probes for all tx router's mac rx side

        NS_LOG_FUNCTION("Mbox installed on router " + to_string(i));
        
        // given flow type infomation
        mboxes.at(i).ip2prot = ip2prot;

        // set weight, rtt, rto & start mbox
        vector<double> rtts;
        for(uint32_t j = 0; j < grp.at(i).txId.size(); j ++)
        {
            double bnDelay = (bottleneckDelay.at(i).c_str()[0] - '0') / 1000.0;
            double dely = (delay[0].c_str()[0] - '0') / 1000.0;
            double nRouter = 3;
            NS_LOG_INFO("rtt[" + to_string(j) + "]: " + to_string(2 *(bnDelay + nRouter * dely)));        // router is 3 now
            rtts.push_back(2 * (bnDelay + 2 * dely) );
        }
        mboxes.at(i).SetWeight(grp.at(i).weight);
        mboxes.at(i).SetRttRto(rtts);
        mboxes.at(i).start();
        
        // tracing
        stringstream ss1;
        if(bypassMacRx)         // by pass the wierd bug of rwnd update: function of MacTx moved to onMacRx
            txRouter->TraceConnectWithoutContext("MacTx", MakeCallback(&MiddlePoliceBox::onMacRx, &mboxes.at(i))); 
        else
        {
            // for(uint32_t j = 0; j < groups.at(i).txId.size(); j ++)         // I think index error is the main cause of mac rx error!!!!
            for(uint32_t j = 1; j <= grp[i].txId.size(); j ++)
            // for(uint32_t j = 1; j < txNode->GetNDevices(); j ++)
            {
                bottleneck.EnablePcapAll("router");
                bool res = txNode->GetDevice(j)->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onMacRx, &mboxes.at(i)));
                ss1 << "       - onMacRx install: " << res << "; addr: " << txNode->GetDevice(j)->GetAddress() << endl;
            }
            txRouter->TraceConnectWithoutContext("MacTx", MakeCallback(&MiddlePoliceBox::onMacTx, &mboxes.at(i)));      // necessary for TCP loss detection
        }
       

        rxRouter->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onPktRx, &mboxes.at(i)));
        qc.Get(2 * i)->TraceConnectWithoutContext("Drop", MakeCallback(&MiddlePoliceBox::onQueueDrop, &mboxes.at(i)));
        txRouter->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onMboxAck, &mboxes.at(i)));    // test passeds
    
        // debug packet path
        for (uint32_t j = 1; j <= grp[i].rxId.size(); j ++)
        {
            bool rxRes = rxNode->GetDevice(j)->TraceConnectWithoutContext("MacTx", MakeCallback(&MiddlePoliceBox::onRouterTx, &mboxes.at(i)));
            ss1 << "    - trace on rx router TX: " << rxRes << endl;
        }
        NS_LOG_DEBUG(ss1.str());


        // debug queue size
        qc.Get(2 * i)->TraceConnectWithoutContext("PacketsInQueue", MakeCallback(&MiddlePoliceBox::TcPktInQ, &mboxes.at(i)));
        qc.Get(2 * i + 1)->TraceConnectWithoutContext("PacketsInQueue", MakeCallback(&MiddlePoliceBox::TcPktInRed, &mboxes.at(i)));
        
        // Ptr<FqTbfQueueDisc> fqp = DynamicCast<FqTbfQueueDisc> (qc.Get(2 * i));      // trial of the interval TBF queue tracing
        Config::ConnectWithoutContext ("/NodeList/*/$ns3::TrafficControlLayer/RootQueueDiscList/*/InternalQueueList/0/TokensInFirstBucket", MakeCallback (&FirstBucketTrace));
        
        // qc.Get(2 * i)->TraceConnectWithoutContext("TokensInFirstBucket", MakeCallback (&FirstBucketTrace));
        // qc.Get(2 * i)->TraceConnectWithoutContext("TokensInSecondBucket", MakeCallback (&SecondBucketTrace));

        Ptr<Queue<Packet>> qp = DynamicCast<PointToPointNetDevice> (txRouter) -> GetQueue();
        qp->TraceConnectWithoutContext("PacketsInQueue", MakeCallback(&MiddlePoliceBox::DevPktInQ, &mboxes.at(i)));


        // trace TCP congestion window and RTT: tested
        for(uint32_t j = 0; j < groups.at(i).txId.size(); j ++)
        {
            string context1 = "/NodeList/0/$ns3::TcpL4Protocol/SocketList/" + to_string(j) + "/CongestionWindow";
            string context2 = "/NodeList/0/$ns3::TcpL4Protocol/SocketList/" + to_string(j) + "/RTT";
            string context3 = "/NodeList/0/$ns3::TcpL4Protocol/SocketList/" + to_string(j) + "/RxAck";
            string context4 = "/NodeList/0/$ns3::TcpL4Protocol/SocketList/" + to_string(j) + "/Latency";
            
            Ptr<Socket> skt = senderApp.at(i * groups[0].N + j)->GetSocket();       // maybe limited for different groups
            bool is_connect[5];
            is_connect[0] = skt->TraceConnect("CongestionWindow", context1, MakeCallback(&MiddlePoliceBox::onCwndChange, &mboxes.at(i)));
            is_connect[1] = skt->TraceConnect("RTT", context2, MakeCallback(&MiddlePoliceBox::onRttChange, &mboxes.at(i)));

            // trace latency and pkt
            // skt->TraceConnectWithoutContext ("RxAck", MakeCallback(&MiddlePoliceBox::onRxAck, &mboxes.at(i)));
            // skt->TraceConnectWithoutContext ("Latency", MakeCallback(&MiddlePoliceBox::onLatency, &mboxes.at(i)));
            is_connect[2] = skt->TraceConnect("RxAck", context3, MakeCallback(&MiddlePoliceBox::onRxAck, &mboxes.at(i)));
            is_connect[3] = skt->TraceConnect("Latency", context4, MakeCallback(&MiddlePoliceBox::onLatency, &mboxes.at(i)));

            NS_LOG_INFO ("Connectivity: " << is_connect[0] << ", " << is_connect[1] << ", " << is_connect[2] << ", " << is_connect[3] << ", " << is_connect[4]);
        }

        // test the rx side device index
        Ptr<Node> rtNode = GetNode(i, grp[i].routerId[2]);
        cout << " rx0: # dev: " << rtNode->GetNDevices() << "; # rx: " << grp.at(i).rxId.size() << endl;
        for(uint32_t k = 0; k < rtNode->GetNDevices() - 2; k ++)
        {
            cout << " -- rx[" << k << "] addr: " << rtNode->GetDevice(k + 1)->GetAddress() << endl;
            cout << " -- rx end[" << k << "] addr: " << GetNode(i, grp.at(i).rxId[k])->GetDevice(0)->GetAddress() << endl;
        }            

        // for debug chain 1
        for(uint32_t k = 0; k < grp.at(i).txId.size(); k ++)
        {
            Ptr<NetDevice> senderDev = GetNode(i, grp[i].txId[k])->GetDevice(0);
            bool b1 = senderDev->TraceConnectWithoutContext("MacTx", MakeCallback(&MiddlePoliceBox::onSenderTx, &mboxes.at(i)));
            // bool b2 = senderDev->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onAckRx, &mboxes.at(i)));
            NS_LOG_DEBUG(" -- Sender debug: MacTx: " << b1 << endl);
        }


        // flow control
        Ptr<QueueDisc> tbfq = qc.Get(2 * i);
        mboxes.at(i).flowControl(mboxes.at(i).GetFairness(), interval, logInterval, ruInterval, tbfq);
    }
}

void RunningModule::disconnectMbox(vector<Group> grp)
{
    NS_LOG_INFO("Disconnect Mbox ... ");

    for(uint32_t i = 0; i < grp.size(); i ++)
    {
        Ptr<NetDevice> txRouter = GetNode(i, grp[i].routerId[0])->GetDevice(0);
        Ptr<NetDevice> rxRouter = GetNode(i, grp[i].routerId[1])->GetDevice(0);
        Ptr<Node> txNode = GetNode(i, grp[i].routerId[0]);
        
        // stop the mbox and tracing
        mboxes.at(i).stop();     // stop flow control and logging 
        
        for(uint32_t j = 1; j <= grp[i].txId.size(); j ++)
            txNode->GetDevice(j)->TraceDisconnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onMacRx, &mboxes.at(i)));
        if(!rxRouter->TraceDisconnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onPktRx, &mboxes[i])))
            NS_LOG_INFO("Failed to disconnect onPktRx!");
        // if(!qc.Get(i)->TraceDisconnectWithoutContext("Drop", MakeCallback(&MiddlePoliceBox::onQueueDrop, &mboxes.at(i))))
        //     NS_LOG_INFO("Failed to disconnect onQueueDrop!");        

        NS_LOG_FUNCTION("Mbox " + to_string(i) + " stops.");
    }
}    

void RunningModule::pauseMbox(vector<Group> grp)
{
    NS_LOG_INFO("Pause Mbox: stop early drop ... ");
    for(uint32_t i = 0; i < grp.size(); i ++)
    {
        Ptr<Node> txNode = GetNode(i, grp[i].routerId[0]);
        Ptr<NetDevice> txRouter = txNode->GetDevice(0);
        txRouter->TraceDisconnectWithoutContext("MacTx", MakeCallback(&MiddlePoliceBox::onMacRx, &mboxes.at(i)));
        txRouter->TraceConnectWithoutContext("MacTx", MakeCallback(&MiddlePoliceBox::onMacRxWoDrop, &mboxes.at(i)));
        // for(uint32_t j = 0; j < txNode->GetNDevices(); j ++)
        // {
        //     txNode->GetDevice(j)->TraceDisconnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onMacRx, &mboxes.at(i)));
        //     txNode->GetDevice(j)->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onMacRxWoDrop, &mboxes.at(i)));
        // }
        
    }
}

void RunningModule::resumeMbox(vector<Group> grp)
{
    NS_LOG_INFO("Resume Mbox: restart early drop ... ");    
    for(uint32_t i = 0; i < grp.size(); i ++)
    {
        Ptr<Node> txNode = GetNode(i, grp[i].routerId[0]);
        // txRouter->TraceDisconnectWithoutContext("MacTx", MakeCallback(&MiddlePoliceBox::onMacTx, &mb));
        // txRouter->TraceConnectWithoutContext("MacTx", MakeCallback(&MiddlePoliceBox::onMacRxWoDrop, &mb));
        for(uint32_t j = 1; j <= grp[i].txId.size(); j ++)
        {
            txNode->GetDevice(j)->TraceDisconnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onMacRxWoDrop, &mboxes.at(i)));
            txNode->GetDevice(j)->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onMacRx, &mboxes.at(i)));
        }
    }
}

void RunningModule::connectMboxCtrl(vector<Group> grp, double interval, double logInterval, double ruInterval)
{
    NS_LOG_FUNCTION("Connect Mbox ... ");
    for(uint32_t i = 0; i < grp.size(); i ++)
    {
        uint32_t nTrace = grp[i].N_ctrl;
        Ptr<NetDevice> txRouter = GetNode(i, grp[i].routerId[0])->GetDevice(0);     // 3 router scenario: txRouter is the 1st one
        Ptr<NetDevice> rxRouter = GetNode(i, grp[i].routerId[2])->GetDevice(0);
        Ptr<Node> txNode = GetNode(i, grp[i].routerId[0]);
        Ptr<Node> rxNode = GetNode(i, grp[i].routerId[2]);
        NetDeviceContainer nc;
        for(uint32_t j = 1; j <= nTrace; j ++)
            nc.Add(txNode->GetDevice(j));               // add rx side devices, this line will add the loop back

        mboxes[i].install(nc);
        NS_LOG_FUNCTION("Mbox installed on router " + to_string(i));
        
        // given flow type infomation
        mboxes.at(i).ip2prot = ip2prot;

        // set weight, rtt, rto & start mbox
        vector<double> rtts;
        for(uint32_t j = 0; j < nTrace; j ++)
        {
            double bnDelay = (bottleneckDelay.at(i).c_str()[0] - '0') / 1000.0;
            double dely = (delay[0].c_str()[0] - '0') / 1000.0;
            double nRouter = 3;
            NS_LOG_INFO("rtt[" + to_string(j) + "]: " + to_string(2 *(bnDelay + nRouter * dely)));        // router is 3 now
            rtts.push_back(2 * (bnDelay + 2 * dely) );
        }
        mboxes.at(i).SetWeight(grp.at(i).weight);
        mboxes.at(i).SetRttRto(rtts);
        mboxes.at(i).start();
        
        // tracing
        stringstream ss1;
        for(uint32_t j = 1; j <= nTrace; j ++)
        // for(uint32_t j = 1; j < txNode->GetNDevices(); j ++)
        {
            bottleneck.EnablePcapAll("router");
            bool res = txNode->GetDevice(j)->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onMacRx, &mboxes.at(i)));
            ss1 << "       - onMacRx install: " << res << "; addr: " << txNode->GetDevice(j)->GetAddress() << endl;
        }
        txRouter->TraceConnectWithoutContext("MacTx", MakeCallback(&MiddlePoliceBox::onMacTx, &mboxes.at(i)));      // necessary for TCP loss detection

        rxRouter->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onPktRx, &mboxes.at(i)));
        qc.Get(2 * i)->TraceConnectWithoutContext("Drop", MakeCallback(&MiddlePoliceBox::onQueueDrop, &mboxes.at(i)));
        qc.Get(2 * i + 1)->TraceConnectWithoutContext("Drop", MakeCallback(&MiddlePoliceBox::onRedDrop, &mboxes.at(i)));
        txRouter->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onMboxAck, &mboxes.at(i)));    // test passeds

        qc.Get(2 * i)->TraceConnectWithoutContext("PacketsInQueue", MakeCallback(&MiddlePoliceBox::TcPktInQ, &mboxes.at(i)));
        qc.Get(2 * i + 1)->TraceConnectWithoutContext("PacketsInQueue", MakeCallback(&MiddlePoliceBox::TcPktInRed, &mboxes.at(i)));
        
        // trace TCP congestion window and RTT: tested
        for(uint32_t j = 0; j < groups.at(i).txId.size(); j ++)
        {
            string context1 = "/NodeList/0/$ns3::TcpL4Protocol/SocketList/" + to_string(j) + "/CongestionWindow";
            string context2 = "/NodeList/0/$ns3::TcpL4Protocol/SocketList/" + to_string(j) + "/RTT";
            string context3 = "/NodeList/0/$ns3::TcpL4Protocol/SocketList/" + to_string(j) + "/RxAck";
            string context4 = "/NodeList/0/$ns3::TcpL4Protocol/SocketList/" + to_string(j) + "/Latency";

            Ptr<Socket> skt = senderApp.at(i * groups[0].N + j)->GetSocket();       // maybe limited for different groups
            bool is_connect[4];
            is_connect[0] = skt->TraceConnect("CongestionWindow", context1, MakeCallback(&MiddlePoliceBox::onCwndChange, &mboxes.at(i)));
            is_connect[1] = skt->TraceConnect("RTT", context2, MakeCallback(&MiddlePoliceBox::onRttChange, &mboxes.at(i)));

            // trace latency and pkt
            is_connect[2] = skt->TraceConnect ("RxAck", context3, MakeCallback(&MiddlePoliceBox::onRxAck, &mboxes.at(i)));
            is_connect[3] = skt->TraceConnect ("Latency", context4, MakeCallback(&MiddlePoliceBox::onLatency, &mboxes.at(i)));
            // is_connect[2] = skt->TraceConnect("RxAck", context3, MakeCallback(&MiddlePoliceBox::onRxAck, &mboxes.at(i)));
            // is_connect[3] = skt->TraceConnect("Latency", context4, MakeCallback(&MiddlePoliceBox::onLatency, &mboxes.at(i)));
            NS_LOG_INFO ("Connectivity: " << is_connect[0] << ", " << is_connect[1] << ", " << is_connect[2] << ", " << is_connect[3]);
        }     

        // flow control
        Ptr<QueueDisc> tbfq = qc.Get(2 * i);
        mboxes.at(i).flowControl(mboxes.at(i).GetFairness(), interval, logInterval, ruInterval, tbfq);
    }

    // NS_LOG_FUNCTION("Connect Mbox Ctrl...");
    // for(uint32_t i = 0; i < grp.size(); i ++)
    // {
    //     Ptr<NetDevice> mbRouter = GetNode(i, grp[i].routerId[0])->GetDevice(0);
    //     Ptr<NetDevice> rxRouter = GetNode(i, grp[i].routerId[2])->GetDevice(0);
    //     Ptr<Node> mbNode = GetNode(i, grp[i].routerId[0]);
    //     NetDeviceContainer nc;
    //     for(uint32_t j = 1; j <= grp[i].N_ctrl; j ++)
    //         nc.Add(mbNode->GetDevice(j));
    //     mboxes[i].install(nc);
    //     mboxes[i].ip2prot = ip2prot;

    //     vector<double> rtts;
    //     for(uint32_t j = 0; j < grp[i].N_ctrl; j ++)
    //     {
    //         double bnDelay = (bottleneckDelay[i].c_str()[0] - '0') / 1000.0;
    //         double dely = (delay.c_str()[0] - '0') / 1000.0;
    //         double nRouter = 3;
    //         rtts.push_back(2 * (bnDelay + 2 * dely));
    //     }
    //     mboxes[i].SetWeight(grp[i].weight);
    //     mboxes[i].SetRttRto(rtts);
    //     mboxes[i].start();

    //     // tracing
    //     cout << "connect: N_ctrl = " << grp[i].N_ctrl << endl;
    //     for(uint32_t j = 1; j <= grp[i].N_ctrl; j ++)
    //     {
    //         bottleneck.EnablePcapAll("router");
    //         bool res = mbNode->GetDevice(j)->TraceDisconnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onMacRx, &mboxes[i]));
    //         NS_ASSERT_MSG(res, "MacRx installation failed at " + to_string(j) + "!");
    //     }

    //     // debug MacRx tracing
    //     cout << "mb dev num: " << mbNode->GetNDevices() << endl;
    //     for(uint32_t j = 1; j <= grp[i].N_ctrl; j ++)
    //     {
    //         cout << mbNode->GetDevice(j)->GetMtu() << ", " << mbNode->GetDevice(j)->GetAddress() << ", ip: ";
    //         GetIpv4Addr(i, grp[i].routerId[0], j).Print(cout);
    //         cout << endl;
    //     }

    //     mbRouter->TraceConnectWithoutContext("MacTx", MakeCallback(&MiddlePoliceBox::onMacTx, &mboxes[i]));
    //     rxRouter->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onPktRx, &mboxes[i]));
    //     qc.Get(2 * i)->TraceConnectWithoutContext("Drop", MakeCallback(&MiddlePoliceBox::onQueueDrop, &mboxes[i]));
    //     mbRouter->TraceConnectWithoutContext("MacRx", MakeCallback(&MiddlePoliceBox::onMboxAck, &mboxes[i]));

    //     qc.Get(2 * i)->TraceConnectWithoutContext("PacketsInQueue", MakeCallback(&MiddlePoliceBox::TcPktInQ, &mboxes[i]));       // debug queue size
    //     qc.Get(2 * i + 1)->TraceConnectWithoutContext("PacketsInQueue", MakeCallback(&MiddlePoliceBox::TcPktInRed, &mboxes[i]));
        
    //     for(uint32_t j = 0; j < groups[i].N_ctrl; j ++)     // trace TCP congestion window and RTT: tested
    //     {
    //         string context1 = "/NodeList/0/$ns3::TcpL4Protocol/SocketList/" + to_string(j) + "/CongestionWindow";
    //         string context2 = "/NodeList/0/$ns3::TcpL4Protocol/SocketList/" + to_string(j) + "/RTT";

    //         Ptr<Socket> skt = senderApp.at(i * groups[0].N + j)->GetSocket();       // maybe limited for different groups
    //         skt->TraceConnect("CongestionWindow", context1, MakeCallback(&MiddlePoliceBox::onCwndChange, &mboxes[i]));
    //         skt->TraceConnect("RTT", context2, MakeCallback(&MiddlePoliceBox::onRttChange, &mboxes[i]));
    //     }

    //     Ptr<QueueDisc> tbfq = qc.Get(2 * i);
    //     mboxes[i].flowControl(mboxes[i].GetFairness(), interval, logInterval, ruInterval, tbfq);
    // }
}

void RunningModule::start()
{
    NS_LOG_FUNCTION ("Start.");   
    sinkApp.Start (Seconds(rtStart));
    sinkApp.Stop (Seconds(rtStop));
    dsCrossApp.Start (Seconds(rtStart));
    dsCrossApp.Stop (Seconds(rtStop));

    for(uint32_t j = 0; j < senderApp.size(); j ++)
    {
        // senderApp.at(j)->SetStartTime(Seconds(rtStart));
        // senderApp.at(j)->SetStopTime(Seconds(rtStop));
        senderApp.at(j)->SetStartTime (Seconds(txStart.at(j)));
        cout << j << ": start at " << txStart.at(j) << endl;
        senderApp.at(j)->SetStopTime (Seconds(txEnd.at(j)));
    }
}

void RunningModule::stop()
{
    NS_LOG_FUNCTION("Stop.");
    for(uint32_t j = 0; j < senderApp.size(); j ++)
        senderApp.at(j)->SetStopTime(Seconds(0.0));     // stop now
    sinkApp.Stop(Seconds(0.0));
}   

int main (int argc, char *argv[])
{

    srand(time(0));
    Packet::EnablePrinting ();          // enable printing the metadata of packet
    Packet::EnableChecking ();
    tokenOut.open ("token_out.dat", ios::out);

    // define the test options and parameteres
    ProtocolType pt = UDP;
    int TCP_var = 1;
    FairType fairness = PRIORITY;
    bool isTrackPkt = true;
    bool isEbrc = false;            // don't use EBRC now, but also want to use loss assignment
    bool isTax = true;              // true: scheme 1, tax; false: scheme 2, counter
    bool isBypass = false;
    bool isMonitor = true;          // now default: stop the mbox
    uint32_t nTx = 3;               // sender number, i.e. link number
    uint32_t nGrp = 1;              // group number
    vector<double> Th;              // threshold of slr/llr
    vector<double> redTh;           // min/max threshold of red
    double minTh = 0, maxTh = 0;
    double slrTh = 0.01;
    double llrTh = 0.01;
    double tStop = 60;
    uint32_t MID1 = 0;
    uint32_t MID2 = 0;
    double alpha;
    uint32_t maxPkts = 1;
    double scale = 5e3;
    int nWeight = 1;                // weight option w cross traffic: 0: fair share; 1: 6:3:1
    int nProtocol = 7;              // No. of scenario like TTT, TTU
    int nTraffic = 0;               // No. of traffic scenario like rate-change, typical
    double estep = 14;
    uint32_t nCross = 0;
    double ncRate = 20;             // unit in Mbps
    double ncType = 1;              // TCP: 1, UDP: 0
    int ncDelay = 2;                // unit in ms
    int nBw = 1000;                  // uint in Mbps
    double nDsBw = 1000;
    double ndsCrossRate = 50;       // unit in Mbps
    double nOnTime = 1;
    double nOffTime = 0;            // unit in s here!

    // specify the TCP socket type in ns-3
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue (1400));   
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4096 * 1024));      // 128 (KB) by default, allow at most 85Mbps for 12ms rtt
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(4096 * 1024));      // here we use 4096 KB
    // Config::SetDefault ("ns3::RedQueueDisc::MaxSize",
                        //   QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, 25)));  // set the unit to packet but not byte
    Config::SetDefault ("ns3::RedQueueDisc::LInterm", DoubleValue (10));              // default 50 -> prob = 0.02, low
    
    double pSize = 1.4 * 8;         // ip pkt size: 1.4 kbit
    vector<double> wt(20, -1.0);

    // command line parameters: focus on slr and llr threshold first, should display in figure name
    CommandLine cmd;
    cmd.AddValue ("tStop", "Stop time of the simulation", tStop);
    cmd.AddValue ("slrTh", "SLR threshold", slrTh);
    cmd.AddValue ("llrTh", "LLR threshold", llrTh);
    cmd.AddValue ("mid", "Mbox 1 ID", MID1);
    cmd.AddValue ("mid2", "Mbox 2 ID", MID2);
    cmd.AddValue ("isTrackPkt", "whether track each packet", isTrackPkt);       // input 0/1
    cmd.AddValue ("cInt", "Control interval of mbox", controlInterval);
    cmd.AddValue ("rInt", "Rate update interval", rateUpInterval);
    cmd.AddValue ("weight1", "weight[1] for 2 weight run", wt[0]);
    cmd.AddValue ("weight2", "weight[2] for 2 weight run", wt[1]);
    cmd.AddValue ("nWeight", "No. of the weights", nWeight);
    cmd.AddValue ("nProtocol", "No. of the protocol", nProtocol);
    cmd.AddValue ("eStep", "Step for UDP exploration", estep);               // explore step of UDP 
    cmd.AddValue ("nTraffic", "No. of the traffic scenario", nTraffic);
    cmd.AddValue ("nCross", "Number of cross traffic", nCross);
    cmd.AddValue ("nGroup", "Number of groups", nGrp);
    cmd.AddValue ("crossRate", "Rate of cross traffic (all the same)", ncRate);
    cmd.AddValue ("crossType", "Type of cross traffic flow (all the same)", ncType);
    cmd.AddValue ("crossDelay", "The delay of cross traffic", ncDelay);
    cmd.AddValue ("isMonitor", "If we only monitor the flows", isMonitor);
    cmd.AddValue ("tcp", "Type of TCP", TCP_var);
    cmd.AddValue ("minTh", "min Th of RED queue", minTh);
    cmd.AddValue ("maxTh", "max Th of RED queue", maxTh);
    cmd.AddValue ("nTx", "Number of senders", nTx);
    cmd.AddValue ("nBw", "Number of Mbps in bottleneck bandwidth", nBw);
    cmd.AddValue ("dsBw", "Number of downstream bandwidth", nDsBw);
    cmd.AddValue ("dsRate", "Downstream cross traffic rate", ndsCrossRate);
    cmd.AddValue ("onTime", "On time for downstream cross traffic", nOnTime);
    cmd.AddValue ("offTime", "Off time of downstream cross traffic", nOffTime);

    cmd.Parse (argc, argv);

    if(!TCP_var) Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpBbr"));
    else if(TCP_var == 1) Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));  
    else if(TCP_var == 2) Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpVeno"));
    else if(TCP_var == 3) Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpScalable"));  

    string bwValue = to_string(nBw) + "Mbps";    

    Th = {slrTh, llrTh};
    if(minTh && maxTh) redTh = {minTh, maxTh};
    // t[1] = tStop;
    cout << "TCP type: " << (TCP_var? "TCP New Reno, etc" : "TCP BBR") << endl;
    cout << "Stop time: " << tStop << endl;
    cout << "MID 1: " << MID1 << endl;
    cout << "MID 2: " << MID2 << endl;
    cout << "Control interval: " << controlInterval << endl;
    cout << "Rate update interval: " << rateUpInterval << endl;
    cout << "Weight: " << wt[0] << " " << wt[1] << endl;
    cout << "nProtocol: " << nProtocol << endl;
    cout << "nTraffic: " << nTraffic << endl;
    cout << "nCross: " << nCross << endl;
    cout << "isMonitor: " << isMonitor << endl;
    cout << "Red min Th: " << minTh << "; max Th: " << maxTh << endl;
    cout << "Bottleneck: " << bwValue << endl;

    // pacing 
    bool if_pacing = false;
    //  -------------- Discard for ns3.27 --------------
    // Config::SetDefault ("ns3::RedQueueDisc::MaxSize", StringValue ("25p"));
    // Config::SetDefault ("ns3::QueueBase::MaxSize", StringValue (std::to_string (maxPkts) + "p"));
    // Config::SetDefault ("ns3::TcpSocketState::EnablePacing", BooleanValue (if_pacing));  // enable pacing for TCP BBR
    cout << "Pacing enabled? " << if_pacing << endl;

    string crossRate = to_string(int(ncRate)) + "Mbps";
    ProtocolType crossType = ncType? TCP : UDP;

    cout << "Cross traffic rate: " << crossRate << endl;
    cout << "Cross traffic type: " << crossType << endl;

    int i = 0;
    while (wt[i] >= 0) i++;
    if (i > 0) nTx = i;

    // define bottleneck link bandwidth and delay, protocol, fairness
    vector<string> bnBw, bnDelay, delays;
    vector<double> onoffTime;
    // string dsBw = to_string(nDsBw) + "Mbps";
    string dsCrossRate = to_string(ndsCrossRate) + "Mbps";
    onoffTime.push_back(nOnTime);
    onoffTime.push_back(nOffTime);

    string crossDelay = to_string(ncDelay) + "ms";
    if(nGrp == 1)
    {
        bnBw = {bwValue};
        // bnBw = {"20Mbps"};
        // bnBw = {"2Mbps"};     // low rate test for queue drop and mac tx
        bnDelay = {"2ms"};
        delays = {"2ms", crossDelay};
        alpha = rateUpInterval / 0.1;     // over the cover period we want
    }
    else if(nGrp == 2)
    {
        bnBw = {"100Mbps", "100Mbps"};
        bnDelay = {"2ms", "2ms"};
        delays = {"2ms", crossDelay};
        alpha = rateUpInterval / 0.1; 
    }

    // generating groups
    cout << "Generating groups of nodes ... " << endl;
    vector<double> weight;
    vector<uint32_t> txId1;
    vector<uint32_t> rtid, rtid2, rxId1, rxId2;
    map<uint32_t, string> tx2rate1, tx2rate2;
    map<uint32_t, ProtocolType> tx2prot1, tx2prot2;
    map<string, uint32_t> rate2port1, rate2port2;
    Group g1, g2;
    vector<Group> grps;

    // define protocol for normal flows: for cases other than cross traffic
    tx2prot1[1] = nProtocol / 4 % 2? TCP : UDP;
    tx2prot1[2] = nProtocol / 2 % 2? TCP : UDP;
    tx2prot1[3] = nProtocol % 2? TCP : UDP;

    if(nCross > 0 && nGrp == 1) // cross traffic settings
    {
        rtid = {1, 2, 3};
        uint32_t tx_offset = 20, rx_offset = 40;
        string normalRate = "100Mbps";
        for (uint32_t i = 0; i < 3 + nCross; i ++)      // typical + cross
        {
            tx2rate1[tx_offset + i] = i < 3? normalRate : crossRate;
            txId1.push_back(tx_offset + i);
            rxId1.push_back(rx_offset + i);
        }
        rate2port1 = {{normalRate, 80}, {crossRate, 90}};
        if (nWeight) weight = {0.6, 0.3, 0.1};
        else weight = {0.333, 0.333, 0.334};
        
        // TCP: 1, UDP: 0
        tx2prot1.clear();
        tx2prot1[tx_offset + 0] = nProtocol / 4 % 2? TCP : UDP;
        tx2prot1[tx_offset + 1] = nProtocol / 2 % 2? TCP : UDP;
        tx2prot1[tx_offset + 2] = nProtocol % 2? TCP : UDP;
        for (uint32_t i = 0; i < nCross; i ++)
            tx2prot1[tx_offset + 3 + i] = crossType;
        
        g1 = Group(rtid, tx2rate1, tx2prot1, rxId1, rate2port1, weight);
        g1.insertLink(txId1, rxId1);
        grps = {g1};

    }
    else if(nTx == 3 && nGrp == 2) // group: 3, 2; for double dumbbell simulations
    {
        rtid = {25, 49, 81};
        tx2rate1 = {{10, "200Mbps"}, {11, "210Mbps"}};
        rxId1 = {2,3};
        rate2port1 = {{"200Mbps", 80}, {"210Mbps", 90}};
        weight = {0.7, 0.3};
        tx2prot1 = {{10, TCP}, {11, TCP}};
        g1 = Group(rtid, tx2rate1, tx2prot1, rxId1, rate2port1, weight);
        g1.insertLink({10, 11}, {2, 3});

        rtid2 = {26, 50, 51};
        tx2rate2 = {{11, "200Mbps"}, {12, "210Mbps"}};
        rxId2 = {3,4};
        rate2port2 = {{"200Mbps", 80}, {"210Mbps", 90}};
        tx2prot2 = {{11, TCP}, {12, TCP}};
        g2 = Group(rtid2, tx2rate2, tx2prot2, rxId2, rate2port2, weight);
        g2.insertLink({11, 12}, {3, 4});
        grps = {g1, g2};
    }
    else if(nTx == 2 && nGrp == 1) // group: 2*1, 1
    {
        rtid = {4, 5, 6};
        tx2rate1 = {{1, "100Mbps"}, {2, "100Mbps"}};
        // tx2rate1 = {{1, "0.01Mbps"}, {2, "20Mbps"}};               // for TCP drop debug only!
        rxId1 = {7, 8};
        rate2port1 = {{"100Mbps", 80}};
        // rate2port1 = {{"0.01Mbps", 80}, {"20Mbps", 90}};           // for TCP drop debug only!
        
        // weight = {0.7, 0.3};
        weight = vector<double> (wt.begin(), wt.begin() + 2);

        tx2prot1 = {{1, TCP}, {2, TCP}};
        g1 = Group(rtid, tx2rate1, tx2prot1, rxId1, rate2port1, weight);      // skeptical
        g1.insertLink({1, 2}, {7, 8});
        grps = {g1};
    }
    else if(nTx == 3 && nGrp == 1) // group: 3, 1
    {
        rtid = {11, 5, 6};
        tx2rate1 = {{1, "100Mbps"}, {2, "100Mbps"}, {3, "100Mbps"}};     // 4: cross traffic here
        // tx2rate1 = {{1, "20Mbps"}, {2, "20Mbps"}, {3, "20Mbps"}};       // 20 Mbps for easy debugging
        rxId1 = {7, 8, 9};
        rate2port1 = {{"100Mbps", 80}};
        // rate2port1 = {{"20Mbps", 80}}; 
        weight = {0.6, 0.3, 0.1};
        // weight = {0.4, 0.3, 0.2, 0.1};
        // tx2prot1[4] = UDP;          // cross traffic flow type

        g1 = Group(rtid, tx2rate1, tx2prot1, rxId1, rate2port1, weight);
        g1.insertLink({1, 2, 3}, {7, 8, 9});
        grps = {g1};
    }
    else if(nTx == 4 && nGrp == 1) // group: 2*2, 1
    {
        rtid = {11, 5, 6};
        tx2rate1 = {{1, "100Mbps"}, {2, "100Mbps"}, {3, "100Mbps"}, {4, "100Mbps"}};
        rxId1 = {7, 8, 9, 10};
        rate2port1 = {{"100Mbps", 80}};
        weight = {0.4, 0.3, 0.2, 0.1};
        tx2prot1 = {{1, TCP}, {2, TCP}, {3, TCP}, {4, TCP}};        // mixed
        g1 = Group(rtid, tx2rate1, tx2prot1, rxId1, rate2port1, weight);
        g1.insertLink({1, 2, 3, 4}, {7, 8, 9, 10});
        grps = {g1};
    }
    else if(nTx == 10)
    {
        rtid = {95, 96, 97};
        for(int i = 0; i < 10; i ++)
        {
            tx2rate1[i] = "100Mbps";
            tx2prot1[i] = TCP;
            txId1.push_back(i);
            rxId1.push_back(i + 10);
        }
        rate2port1 = {{"100Mbps", 80}};
        weight = {0.5, 0.055, 0.055, 0.055, 0.055, 0.056, 0.056, 0.056, 0.056, 0.056}; 
        g1 = Group(rtid, tx2rate1, tx2prot1, rxId1, rate2port1, weight);
        g1.insertLink(txId1, rxId1);
        grps = {g1};
        scale = 2e3;
    }
    else if (nGrp == 1)
    {
        rtid = {95, 96, 97};
        for(int i = 0; i < nTx; i ++)
        {
            tx2rate1[i] = "100Mbps";
            tx2prot1[i] = TCP;
            txId1.push_back(i);
            rxId1.push_back(i + nTx);
        }
        rate2port1 = {{"100Mbps", 80}};
        weight.push_back(0.5);
        for(int i = 0; i < nTx - 1; i ++)
            weight.push_back(0.5 / (nTx - 1) );
        g1 = Group(rtid, tx2rate1, tx2prot1, rxId1, rate2port1, weight);
        g1.insertLink(txId1, rxId1);
        grps = {g1};
        scale = 2e3;
    }

    // set start and stop times
    uint32_t N;
    for (uint32_t i = 0; i < grps.size(); i ++)
        N += grps[i].txId.size();
    vector<double> t(2 + N * 2);
    t[0] = 0.0;
    t[1] = tStop;
    t[2] = 0.0;             // flow 0 start later than other flows
    
    switch (nTraffic)
    {
    case 0:     // Normal case: stop at specified time
        for (uint32_t i = 3; i < 2*N + 2; i ++)
        {
            if (i > N + 1) t[i] = tStop;
            else t[i] = 0.0;
        }
        break;
    case 1:     // rate change test
        for(uint32_t i = 3; i < 2*N + 2; i ++)      // Debug: 0, 10, 20s
        {
            if(i < N + 2) t[i] = (i - 2) * 10;
            else t[i] = tStop - (2*N + 1 - i) * 10;
            cout << i << ": " << t[i] << endl;
        }
        break;
    case 2:     // typical test: 0,50,100; 400,450,500
        for (uint32_t i = 3; i < 2*N + 2; i ++)
        {
            double step = 50.0;
            if(i > N + 1) t[i] = tStop - (2*N + 1 - i) * step;
            else t[i] = (i - 2) * step;
        }
        break;
    case 3:     // fast rate change simulation, tStop = 30s
        for(uint32_t i = 3; i < 2*N + 2; i ++)
        {
            if(i < N + 2) t[i] = (i - 2) * 6;
            else t[i] = tStop - (2*N + 1 - i) * 6;
        }
    case 4:     // cross traffic rate change => mbox's capacity changes
        for(uint32_t i = 3; i < 2*N + 2; i ++)
        {
            if(i < 5) t[i] = 0;
            else if (i < N + 2) t[i] = (i - 4) * 10;
            else if (i < N + 5) t[i] = tStop;
            else t[i] = tStop - (2*N + 1 - i) * 10;
        }
    default:
        break;
    }

    // running module construction
    LogComponentEnable("RunningModule", LOG_LEVEL_INFO);
    LogComponentEnable("MiddlePoliceBox", LOG_INFO);
    // LogComponentEnable("FqTbfQueueDisc", LOG_INFO);
    // LogComponentEnable("QueueDisc", LOG_FUNCTION);

    cout << "Initializing running module..." << endl;
    RunningModule rm(t, grps, pt, bnBw, bnDelay, delays, nDsBw, {isTrackPkt, isBypass}, 1400);
    cout << "Building topology ... " << endl;
    rm.buildTopology(grps);

    // mbox construction
    cout << "Configuring ... " << endl;
    // vector<MiddlePoliceBox> mboxes;
    MiddlePoliceBox mbox1, mbox2;
    double beta = 0.98;
    vector<uint32_t> num;
    if(nGrp == 1 && nTx == 4)           // limitation: current mbox could only process 2 rate level!
        num = vector<uint32_t> {4,4,2,2};
    else if(nGrp == 1 && nTx == 3) 
        // num = vector<uint32_t> {3,3,2,1};
    {
        num = grps[0].mNum(2);
        for(auto e : num)
            cout << e << " ";
        cout << endl;
        vector<uint32_t> consNum = {4, 3, 2, 1};
        // NS_ASSERT_MSG(num == consNum, "Typical traffic setting is wrong!");
    }
    else if(nTx == 10) 
        num = vector<uint32_t> {10,10,1,9};
    else if(nTx == 2)
        num = vector<uint32_t> {2,2,1,1};
    else if(nGrp == 1)
        num = vector<uint32_t> {nTx, nTx, 1, nTx - 1};
    else if (nTx == 3)
        num = vector<uint32_t> {2, 2, 1, 1};
    mbox1 = MiddlePoliceBox(num, t[1], pt, fairness, pSize, isTrackPkt, beta, Th, MID1, 50, {isEbrc, isTax, isBypass, isMonitor}, alpha, scale, estep);         // vector{nSender, nReceiver, nClient, nAttacker}

    cout << "Mbox 1 initialized!" << endl;

    vector<MiddlePoliceBox> mboxes({mbox1});
    if(nGrp == 2) 
    {
        mbox2 = MiddlePoliceBox(vector<uint32_t>{2,2,1,1}, t[1], pt, fairness, pSize, isTrackPkt, beta, Th, MID2, 50, {isEbrc, isTax, isBypass, isMonitor}, alpha, scale, estep);
        mboxes.push_back(mbox2);
        cout << "Mbox 2 initialized!" << endl;
    }
    rm.configure(t[1], pt, bnBw, dsCrossRate, onoffTime, bnDelay, mboxes, redTh);

    // // test pause, resume and disconnect mbox
    // Simulator::Schedule(Seconds(5.1), &RunningModule::disconnectMbox, &rm, grps);
    // Simulator::Schedule(Seconds(8.1), &RunningModule::connectMbox, &rm, grps, 1.0, 1.0);
    // Simulator::Schedule(Seconds(0.01), &RunningModule::pauseMbox, &rm, grps);
    // Simulator::Schedule(Seconds(1.01), &RunningModule::resumeMbox, &rm, grps);

    // flow monitor
    Ptr<FlowMonitor> flowmon;
    FlowMonitorHelper flowmonHelper;
    flowmon = flowmonHelper.InstallAll ();
    flowmon->Start (Seconds (0.0));
    flowmon->Stop (Seconds (t[1]));
    
    // run the simulation
    cout << "Begin populate routing tables ... " << endl;
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    cout << " Start running ... " << endl << endl;
    Simulator::Stop(Seconds(t[1]));
    Simulator::Run();

    cout << " MID: " << MID1 << "," << MID2 << " Destroying ..." << endl << endl;
    flowmon->SerializeToXmlFile ("mrun.flowmon", false, false);
    tokenOut.close();
    Simulator::Destroy();

    return 0;
}
