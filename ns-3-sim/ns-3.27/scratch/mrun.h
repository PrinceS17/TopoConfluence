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

#ifndef MRUN_H
#define MRUN_H

// #include "ns3/mbox.h"
#include "ns3/mbox-module.h"

using namespace std;
using namespace ns3;

namespace ns3 {

// NS_LOG_COMPONENT_DEFINE ("RunningModule");

/**
 * Capability Helper at receiver side, which can help get the tag No. from UDP packet, set ack No.
 * for capability (of UDP), set the app for sending back ack and send ack back on RX. Members include
 * RX net device (for tracing), flow ID (node No.), ack socket, ack sending app. 
 */
class CapabilityHelper
{
public:
    CapabilityHelper() = default;
    CapabilityHelper(uint32_t flow_id, Ptr<Node> node, Ptr<NetDevice> device, Address addr);// given flow ID and net device, initialize the socket and app for sending ACK
    // CapabilityHelper(const CapabilityHelper &);
    // CapabilityHelper & operator = (const CapabilityHelper);
    // ~CapabilityHelper();                // need to form a vector
    
    vector<int> GetNumFromTag(Ptr<const Packet> p);                    // given packet, extract the flow id and seq No.
    void SendAck(Ptr<const Packet> p);                                           // given Ack No. send back one ACK
    void install(uint32_t flow_id, Ptr<Node> node, Ptr<NetDevice> device, Address addr);    // similar to ctor, set the tracing for on RX and set the app
    uint32_t getFlowId();
    vector<uint32_t> getCurAck();           // use the MyApp's value

private:
    uint32_t flow_id;
    uint32_t curAckNo;
    Ptr<PointToPointNetDevice> device;
    Ptr<MyApp> ackApp;
};


// should first set before building the topology
class Group
{
public:
    Group() = default;
    Group(vector<uint32_t> rtid, map<uint32_t, string> tx2rate1, map<uint32_t, ProtocolType> tx2prot1, vector<uint32_t> rxId1, map<string, uint32_t> rate2port1, vector<double> w = vector<double>()):
        routerId(rtid), tx2rate(tx2rate1), tx2prot(tx2prot1), rxId(rxId1), rate2port(rate2port1)
        {
            // some direct visit exist? no at least from stackoverflow
            // need unit testing!
            for(pair<uint32_t, string> pid:tx2rate)
            {
                // txId and rates vector construction
                if(find(txId.begin(), txId.end(), pid.first) == txId.end())
                    txId.push_back(pid.first);
                if(find(rates.begin(), rates.end(), pid.second) == rates.end())     // need testing
                    rates.push_back(pid.second);

                // rate 2 tx map initialization
                if(rate2tx.find(pid.second) == rate2tx.end())
                    rate2tx[pid.second] = vector<uint32_t>();
                rate2tx[pid.second].push_back(pid.first);
            }

            N = txId.size() + rxId.size() + 3;
            N_tx = txId.size();

            if(w.size() > 0) weight = w;
            else weight = vector<double> (txId.size(), 1.0 / (double)txId.size());

            N_ctrl = weight.size();

            for(pair<string, uint32_t> pid:rate2port)
            {
                if(find(ports.begin(), ports.end(), pid.second) == ports.end())
                    ports.push_back(pid.second);
            }
            /* Print path vector to console */
            cout << endl << "========== Group info ============" << endl;
            cout << "TX ID: ";
            copy(txId.begin(), txId.end(), ostream_iterator<uint32_t>(cout, " "));
            cout << "\nRX ID: ";
            copy(rxId.begin(), rxId.end(), ostream_iterator<uint32_t>(cout, " "));
            cout << "\nrates vector: ";
            copy(rates.begin(), rates.end(), ostream_iterator<string>(cout, " "));
            cout << "\nports: ";
            copy(ports.begin(), ports.end(), ostream_iterator<uint32_t>(cout, " "));
            cout << "\nprotocols: ";
            for (auto tp:tx2prot) cout << tp.first << ": " << (tp.second == TCP? "TCP":"UDP") << ", ";
            cout << "\nN: " << N << endl << endl;
        }
    Group(const Group &) = default;
    ~Group() {}
    void insertLink(uint32_t tx, uint32_t rx)
    {
        tx2rx.insert(pair<uint32_t, uint32_t>(tx, rx));
    }
    /* links: txs[i] -> rxs[i] */
    void insertLink(vector<uint32_t> txs, vector<uint32_t> rxs)
    {
        for(uint32_t i = 0; i < txs.size(); i ++)
            insertLink(txs.at(i), rxs.at(i));
    }
    vector<uint32_t> mNum(uint32_t nNormal)         // return: {nSender, nCtrl, nNormal, nAttack}, where nSender >= nCtrl = nNormal + nAttack
    {
        return {N_tx, N_ctrl, nNormal, N_ctrl - nNormal};
    }

public:
    uint32_t N;                 // number of all nodes including router
    uint32_t N_tx;              // number of TX nodes
    uint32_t N_ctrl;            // number of flows controlled
    // vector<uint32_t> nodeId;  // collection of all nodes with the same mbox (except routers), seems not needed now??
    vector<uint32_t> routerId;  // router ID, [tx router, rx router]
    vector<uint32_t> txId;
    vector<uint32_t> rxId;
    vector<string> rates;       // collection all rate in this group
    vector<uint32_t> ports;

    multimap <uint32_t, uint32_t> tx2rx;        // tx-rx node map: m-in-n-out map, test m!=n in later version
    map <string, vector<uint32_t>> rate2tx;     // sort each node by its data rate (like client, attacker before)
    map <uint32_t, string> tx2rate;             // tx node to rate
    map <uint32_t, ProtocolType> tx2prot;       // tx node to transport protocol 
    map <string, uint32_t> rate2port;           // port for every rate level (abstract for client and attacker)

    vector <double> weight;                     // weight for each sender (current version)
};

class RunningModule
{
public:
    /**
     * \brief Initialize the running module, including setting start and stop time, the 
     * bottleneck link, the data rates and the topology. Currently should use dumpbell to
     * realize the symmetric topology. 
     * 
     * \param t Vector containing start and stop time.
     * \param grp Vector containing node group which specifies their rates and mboxes.
     * \param pt Protocal type, TCP or UDP.
     * \param bnDelay Bottleneck link delay.
     * \param delay Leaf delay: normal delay, cross traffic delay.
     * \param dsBw Downstream bandwidth.
     * \param rate Vector of different level of data rate, e.g. {1kbps, 10kbps, 1Mbps} 
     * for three groups.
     * \param size Packet size, 1000 B by default.
     */
    RunningModule(vector<double> t, vector<Group> grp, ProtocolType pt, vector<string> bnBw, vector<string> bnDelay, vector<string> delay, double dsBw, vector<bool> fls = {false, true}, uint32_t size = 1000);
    ~RunningModule ();
    /**
     * \brief Build the network topology from link (p2p) to network layer (stack). p2p link 
     * attributes should be carefully set (not including queue setting and IP assignment). 
     * Note the routerId of Group should be completed.
     * 
     * \param grp Vector including node group with rate level.
     */
    void buildTopology(vector<Group> grp);
    /**
     * \brief Configure all the network entities after finishing building the topology. The process
     * is: queue, ip assignment (in setQueue), sink app and sender app setting, mbox installation, start 
     * and stop the module. Note that all the argument of configure take effect.
     * 
     * \param stopTime Relative stop time of this run.
     * \param pt Transport Protocol.
     * \param bw Vector of the bottleneck links.
     * \param delay Delay of the links. (to be determined)
     * \param Th In format [MinTh, MaxTh].
     */ 
    void configure(double stopTime, ProtocolType pt, vector<string> bw, string dsCrossRate, vector<double> onoffTime, vector<string> delay, vector<MiddlePoliceBox>& mboxes, vector<double> Th=vector<double>());
    /**
     * \brief Set queue (RED by default, may need other function for other queue) and return 
     * a queue disc container for tracing the packet drop by RED queue. Later assign Ipv4 
     * addresses for all p2p net devices.
     * 
     * \param grp Vector including node group with rate level.
     * \param Th In format [MinTh, MaxTh], if empty, then use ns-3 default.
     * \param bw Bottleneck Link bandwidth collection.
     * \param delay Bottleneck link delay.
     * 
     * \returns A queue disc container on router that we are interested in.
     */
    QueueDiscContainer setQueue(vector<Group> grp, vector<string> bw, vector<string> delay, vector<double> Th=vector<double>());
    /**
     * \brief Set prio queue (2 RED), one for priviledged. No use now.
     * 
     * \param grp Vector including node group with rate level.
     * \param Th In format [MinTh, MaxTh], if empty, then use ns-3 default.
     * \param bw Bottleneck Link bandwidth collection.
     * \param delay Bottleneck link delay.
     * 
     * \returns A queue disc container on router that we are interested in.
     */
    // QueueDiscContainer setPrioQueue(vector<Group> grp, vector<string> bw, vector<string> delay, vector<double> Th=vector<double>());
    /**
     * \brief Set the address for sender, receiver and router.
     * 
     * \returns A ipv4 internet container for later specifying destination's address.
     */
    Ipv4InterfaceContainer setAddress();
    /**
     * \brief Set the sink application (fetch protocol from member)
     * 
     * \param Node group with rate level (containing port for different rate).
     * 
     * \returns An application container for all sink apps.
     */
    ApplicationContainer setSink(vector<Group> grp, ProtocolType pt);
    /**
     * \brief Set the capability helper at the receiver side.
     * 
     * \param Node group with rate level (containing port for different rate).
     * 
     * \returns A vector of all capability helpers.
     */
    vector< CapabilityHelper > setCapabilityHelper(vector<Group> grp);
    /**
     * \brief Set the sender application (may set from outside)
     * 
     * \param Node group with rate level (containing port for different rate).
     * \param pt Protocol type, TCP or UDP;
     * 
     * \returns A collection of all the pointer of MyApp.
     */
    vector< Ptr<MyApp> > setSender(vector<Group> grp, ProtocolType pt);
    /**
     * \brief Set one single network flow from sender to sink. Basically called by setSender.
     * Note rate and port are already specified in member groups.
     * 
     * \param i Group id.
     * \param tId Tx id in the group of the flow.
     * \param rId Rx id in the group of the flow.
     * \param tag Tag value attached to this flow.
     * 
     * \returns A pointer to this application.
     */
    Ptr<MyApp> netFlow(uint32_t i, uint32_t tId, uint32_t rId, uint32_t tag);
    /**
     * \brief Set up downstream cross traffic for each leaf link, to make the traffic more 
     * realistic.
     * 
     * \param grp Groups.
     * \returns Container for the generated onoff applications.
     */
    ApplicationContainer setDsCross(vector<Group> grp);
    /**
     * \brief Connect to the installed mboxes and begin tracing.
     * 
     * \param grp Node group with rate level.
     * \param interval Interval of mbox's detection.
     * \param logInterval Interval of mbox's logging for e.g. data rate, llr, slr.
     * \param ruInterval Interval for real time tx & Ebrc rate update.
     */
    void connectMbox(vector<Group> grp, double interval, double logInterval, double ruInterval);
    /**
     * \brief Stop the installed mboxes and disconnect the tracing.
     * 
     * \param grp Node group with rate level.
     */
    void disconnectMbox(vector<Group> grp);
    /**
     * \brief Pause the mbox, i.e. stop control (early drop) but continue detecting packets.
     * 
     * \param grp Node group with rate level.
     */
    void pauseMbox(vector<Group> grp);
    /**
     * \brief Resume the mbox, i.e. continue to both detect and drop the packets.
     * 
     * \param grp Node group with rate level.
     */
    void resumeMbox(vector<Group> grp);
    /**
     * \brief Connect to the mbox given the existance of cross traffic.
     * 
     * \param grp Node group with rate level.
     * \param interval Interval of mbox's detection.
     * \param logInterval Interval of mbox's logging for e.g. data rate, llr, slr.
     * \param ruInterval Interval for real time tx & Ebrc rate update.
     */
    void connectMboxCtrl(vector<Group> grp, double interval, double logInterval, double ruInterval);    
    /**
     * \brief Start all the application from Now() and also start the mbox detection by tracing.
     */
    void start();
    /**
     * \brief Stop all the application from Now() and also stop the mbox detection by disconnecting 
     * tracing.
     */ 
    void stop();

    /**
     * \brief Create node given group and id.
     * 
     * \param i The index of group in groups.
     * \param id Node id in group g.
     * \return Index of nodes.
     */
    uint32_t SetNode(uint32_t i, uint32_t id);
    /**
     * \brief Create node given group and n (inside group No. ).
     * 
     * \param type The type of node: sender/receiver/router: 0/1.
     * \param i The index of group in groups.
     * \param n The inside group No. 
     * \return Index of nodes.
     */
    uint32_t SetNode(uint32_t type, uint32_t i, uint32_t n);
    /**
     * \brief Set the pointer of router into node.
     * 
     * \param 
     */
    uint32_t SetRouter(Ptr<Node> pt, uint32_t i, uint32_t id);
    /**
     * \brief Get node from group and id. Should be exactly inverse of SetNode();
     * 
     * \param i The index of group in groups.
     * \param id Node id in group g.
     */
    Ptr<Node> GetNode(uint32_t i, uint32_t id);
    /**
     * \brief Get ipv4 address for socket destination setting.
     * 
     * \param i The index of group in groups.
     * \param k The No. of the device installed on that node (maybe larger than 0).
     * \param id Node id in group g. 
     */
    Ipv4Address GetIpv4Addr(uint32_t i, uint32_t id, uint32_t k = 0);
    void txSink(Ptr<const Packet> p);   // !< for test and debug
    void onCwndChange(string context, uint32_t oldValue, uint32_t newValue);
    uint32_t GetId();
    // static void onCwndChangeWo(uint32_t oldValue, uint32_t newValue);
    
public:         // network entity
    NodeContainer nodes;        // all nodes in the topology
    NodeContainer routers;
    NetDeviceContainer txDevice;
    NetDeviceContainer rxDevice;
    NetDeviceContainer txRouterDevice;
    NetDeviceContainer rxRouterDevice;
    NetDeviceContainer routerDevice;

    map< uint32_t, uint32_t > id2nodeIndex;     // mapping from node ID (tx/rx/rt ID) to the index in NodeContainer
    
    QueueDiscContainer qc;          // queue container for trace
    Ipv4InterfaceContainer ifc;     // ipv4 interface container for flow destination specification
    map< pair<uint32_t,uint32_t>, uint32_t > id2ipv4Index;     // mapping from node ID and device No. to index of ipv4 container
    
    map<Ipv4Address, ProtocolType> ip2prot;

    ApplicationContainer sinkApp, dsCrossApp;   // sink app
    vector< CapabilityHelper > chelpers;    // capability helpers on RX nodes
    vector< Ptr<MyApp> > senderApp;   // sender app: need testing!

    vector<PointToPointDumbbellHelper> dv;  // use to preserve channel information
    vector<MiddlePoliceBox> mboxes;         // use to make the mboxes consistent

    PointToPointHelper bottleneck;

private:        // parameters
    // basic
    uint32_t ID;
    uint32_t nSender;
    uint32_t nReceiver;
    vector<Group> groups;       // group node by different mbox: need testing such vector declaration

    uint32_t pktSize;           // 1000 kB
    uint32_t u;                 // unit size of group leaves
    double rtStart;             // start time of this run (the initial one)
    double rtStop;              // stop time of this run
    vector<double> txStart;     // start time of TX flows 
    vector<double> txEnd;       // end time of Tx flows
    ProtocolType protocol;
    
    // queue
    string qType = "RED";       // specified for queue disc
    double minTh = 100;
    double maxTh = 200;

    // link related
    string normalBw = "1Gbps";
    vector<string> bottleneckBw = vector<string>();
    vector<string> bottleneckDelay = vector<string>();
    vector<string> delay;
    double downstreamBw;
    string dsCrossRate = "0bps";
    vector<double> onoffTime = {1, 0};
    string mtu = "1599";        // p2p link setting

    bool isTrackPkt;
    bool bypassMacRx;
    vector<string> fnames;

};

}

#endif