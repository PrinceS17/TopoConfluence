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
 */

/*
This program is used to simulate different network topologies with multiple normal
flows and cross traffics. The parameters that can be set include:

Number of our monitored flow (refered as "normal flow");
number of cross traffic;
sending rate of normal flow; 
sending rate of cross traffic;
channel rate of edge link (CSMA by default);
configure file of BRITE topology;
run ID (determine the topology random stream).

The bandwidth of bottleneck, upstream & downstream link are all configured in 
BRITE conf file, i.e. interBW for bottleneck and intraBW for downstream. Note that
though this parameters as well as the topology are not set in this program, here
we should still check the compatibility between the topology and flow. The error
caused by insufficient nodes is quite common.

This program is not designed for emulation, but it is expected to support emulation
as much as possible. Only changing the flow application is ideal for emulation.
*/

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include "ns3/minibox-module.h"
#include "ns3/ratemonitor-module.h"
#include "ns3/brite-module.h"

using namespace std;
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("BriteForAll");

int main (int argc, char *argv[])
{
    uint32_t verbose = 2;
    srand (time (0));
    uint32_t mid = rand() % 10000;
    uint32_t tid = 9563;
    uint32_t nNormal = 3;
    uint32_t nCross = 0;
    uint32_t nDsForEach = 0;
    uint32_t normalRate = 100;
    uint32_t crossRate = 5;
    uint32_t edgeRate = 8000;
    uint32_t dsCrossRate = 30;
    double tStop = 2;                   // 2s only for test
    string confFile = "brite_conf/TD_CustomWaxman.conf";

    CommandLine cmd;
    cmd.AddValue ("v", "Enable verbose", verbose);
    cmd.AddValue ("mid", "Run ID (4 digit)", mid);
    cmd.AddValue ("tid", "Topology stream ID", tid);
    cmd.AddValue ("nNormal", "Number of normal flows", nNormal);
    cmd.AddValue ("nCross", "Number of cross traffic", nCross);
    cmd.AddValue ("nDsForEach", "Downstream flow number for each destination", nDsForEach);
    cmd.AddValue ("normalRate", "Rate of normal flow", normalRate);
    cmd.AddValue ("crossRate", "Rate of cross traffic", crossRate);
    cmd.AddValue ("edgeRate", "Rate of edge link (in Mbps only)", edgeRate);
    cmd.AddValue ("dsCrossRate", "Rate of downstream flow", dsCrossRate);
    cmd.AddValue ("tStop", "Time to stop simulation", tStop);
    cmd.AddValue ("confFile", "path of BRITE configure file", confFile);
    cmd.Parse (argc, argv);

    normalRate *= 1000000;
    crossRate *= 1000000;
    dsCrossRate *= 1000000;

    if (verbose > 0) 
    {
        LogComponentEnable ("BriteForAll", LOG_LEVEL_INFO);
        LogComponentEnable ("RateMonitor", LOG_LEVEL_INFO);
    }
    if (verbose > 1) LogComponentEnable ("MiniBox", LOG_LEVEL_INFO);
    
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue (1400));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4096 * 1024));      // 128 (KB) by default, allow at most 85Mbps for 12ms rtt
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(4096 * 1024));      // here we use 4096 KB
    stringstream ss;
    ss << "-> Parameters parsed.\n  verbose: " << verbose << "\n  mid: " << mid << "\n  tid: " << tid << "\n  nNormal: " \
        << nNormal << "\n  nCross: " << nCross << "\n  normal rate: " << normalRate << "\n  cross rate: " \
        << crossRate << "\n  edge rate: " << edgeRate << " Mbps\n  stop time: " << tStop << "\n  BRITE conf. file:" \
        << confFile << endl;
    NS_LOG_INFO (ss.str ());
    ss.str ("");

    // build BRITE core topology
    BriteTopologyHelper bth (confFile);
    bth.AssignStreams (tid % 100);
    InternetStackHelper stack;
    Ipv4AddressHelper addr ("10.0.0.0", "255.255.255.252");
    bth.BuildBriteTopology (stack);
    bth.SetQueue ();                        // set RED queue with path in brite-topology-helper!
    bth.AssignIpv4Addresses (addr);
    ss << "-> BRITE topology built. Number of ASes: " << bth.GetNAs () << endl;
    ss << "  Number of routers and leaves: " << endl;
    for (uint32_t i = 0; i < bth.GetNAs (); i ++)
        ss << "  - AS " << i << ": " << bth.GetNNodesForAs (i) << " nodes, " << \
            bth.GetNLeafNodesForAs (i) << " leaves;" << endl;
    NS_LOG_INFO (ss.str());
    ss.str("");
    
    for (uint32_t i = 0; i < bth.GetNAs (); i ++)       // topology leaf check
        NS_ASSERT_MSG (bth.GetNLeafNodesForAs (i) >= nNormal + nCross, "-> AS " + \
            to_string(i) + " doesn't have enough leaves, please change configure file!" );
    
    // set up normal flows and cross traffic
    uint32_t txAS = 0, rxAS = 1;
    NodeContainer txEnds, rxEnds;               // rxEnds: for downstream use, only containes normal flow ends
    NetDeviceContainer txEndDevices, rxEndDevices;
    Ipv4AddressHelper leftAddr ("10.1.1.0", "255.255.255.0");
    Ipv4AddressHelper rightAddr ("10.2.1.0", "255.255.255.0");
    for (uint32_t i = 0; i < nNormal; i ++)
    {
        Ptr<Node> txLeaf = bth.GetLeafNodeForAs (txAS, i);
        Ptr<Node> rxLeaf = bth.GetLeafNodeForAs (rxAS, i);
        Flow normalFlow (txLeaf, rxLeaf, leftAddr, rightAddr, normalRate, {0, tStop});
        normalFlow.build (to_string (edgeRate) + "Mbps");
        normalFlow.setOnoff ();
        leftAddr = normalFlow.getLeftAddr ();
        rightAddr = normalFlow.getRightAddr ();
        txEnds.Add (normalFlow.getHost (0));
        rxEnds.Add (normalFlow.getHost (1));
        txEndDevices.Add (normalFlow.getEndDevice (0));
        rxEndDevices.Add (normalFlow.getEndDevice (3));
    }
    NS_LOG_INFO ("-> Normal flows set.");

    for (uint32_t i = nNormal; i < nNormal + nCross; i ++)
    {
        Ptr<Node> txLeaf = bth.GetLeafNodeForAs (txAS, i);
        Ptr<Node> rxLeaf = bth.GetLeafNodeForAs (rxAS, i);
        Flow crossTraffic (txLeaf, rxLeaf, leftAddr, rightAddr, crossRate, {0, tStop});
        crossTraffic.build (to_string (edgeRate) + "Mbps");
        crossTraffic.setOnoff ();
        leftAddr = crossTraffic.getLeftAddr ();
        rightAddr = crossTraffic.getRightAddr ();
        txEnds.Add (crossTraffic.getHost (0));
        txEndDevices.Add (crossTraffic.getEndDevice (0));
        rxEndDevices.Add (crossTraffic.getEndDevice (3));
    }
    NS_LOG_INFO ("   Cross traffic set.");

    uint32_t offset = nNormal + nCross;
    for (uint32_t i = 0; i < nNormal; i ++)
    for (uint32_t j = 0; j < nDsForEach; j ++)
    {
        uint32_t idx = offset + i * nDsForEach + j;
        Ptr<Node> txLeaf = bth.GetLeafNodeForAs (rxAS, idx);        // can also use just (rxAS, i) to simplify
        Ptr<Node> rxLeaf = rxEnds.Get (i);
        Flow downstreamFlow (txLeaf, rxLeaf, leftAddr, rightAddr, dsCrossRate, {0, tStop});
        downstreamFlow.build (to_string (edgeRate) + "Mbps");
        downstreamFlow.setOnoff ();
        leftAddr = downstreamFlow.getLeftAddr ();
        rightAddr = downstreamFlow.getRightAddr ();
        txEnds.Add (downstreamFlow.getHost (0));
        txEndDevices.Add (downstreamFlow.getEndDevice (0));
        rxEndDevices.Add (downstreamFlow.getEndDevice (3));
    }
    NS_LOG_INFO ("   Downstream cross traffic set.");

    // set up minibox & rate monitor for data collection
    vector<Ptr<MiniBox>> mnboxes;
    vector<Ptr<RateMonitor>> mons;
    Time t0 (Seconds (0.01)), t1 (Seconds (tStop));
    for (uint32_t i = 0; i < nNormal + nCross + nDsForEach * nNormal; i ++)
    {
        vector<uint32_t> id = {mid, i};
        Ptr<MiniBox> mnbox = CreateObject <MiniBox, vector<uint32_t>> (id);
        mnboxes.push_back (mnbox);
        mnbox->install (txEnds.Get (i), txEndDevices.Get (i));
        mnbox->start (t0);
        mnbox->stop (t1);

        Ptr<RateMonitor> mon = CreateObject <RateMonitor, vector<uint32_t>, bool> (id, true);
        mons.push_back (mon);
        mon->install (rxEndDevices.Get (i));
        // mon->install (txEndDevices.Get (i));         // test tx sending rate
        mon->start (t0);
        mon->stop (t1);
    }
    NS_LOG_INFO ("-> MiniBox and RateMonitor set. \n");

    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    Simulator::Stop (Seconds (tStop));
    Simulator::Run ();
    Simulator::Destroy ();
    return 0;
}