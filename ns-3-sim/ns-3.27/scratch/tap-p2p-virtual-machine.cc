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

//
//  +----------+                                             +----------+
//  | virtual  |                                             | virtual  |
//  |  Linux   |                                             |  Linux   |
//  |   Host   |                                             |   Host   |
//  |          |                                             |          |
//  |   eth0   |                                             |   eth0   |
//  +----------+                                             +----------+
//       |                                                        |
//  +----------+                                             +----------+
//  |  Linux   |                                             |  Linux   |
//  |  Bridge  |                                             |  Bridge  |
//  +----------+                                             +----------+
//       |                                                        |
//  +------------+                                         +-------------+
//  | "tap-left" |                                         | "tap-right" |
//  +------------+                                         +-------------+
//       |           n0                              n3           |
//       |       10.1.1.1                        10.1.3.2
//       |       +--------+                      +--------+       |
//       +-------|  tap   |                      |  tap   |-------+
//               | bridge |                      | bridge |
//               +--------+                      +--------+
//               |  CSMA  |                      |  CSMA  |
//               +--------+                      +--------+
//                   |                               |
//                   |                               |
//                   ========  n1 -------- n2  =======
//                     CSMA         p2p         CSMA
//
#include <iostream>
#include <fstream>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/tap-bridge-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/flow-monitor-helper.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("TapP2pVirtualMachine");

int cnt1 = 0, cnt2 = 0, cnt3 = 0;

void LeftReceived(Ptr<const Packet> p)
{
    NS_LOG_DEBUG("Left RX: count = " << cnt1++);
}

void RightReceived(Ptr<const Packet> p)
{
    NS_LOG_DEBUG("Right RX: count = " << cnt2++);
}

void LeftMacTx(Ptr<const Packet> p)
{
    NS_LOG_FUNCTION("Left TX: count = " << cnt3++);
}

int 
main (int argc, char *argv[])
{
    int nRate = 10, nDelay = 5;
    double tStop = 600.0;
    CommandLine cmd;
    cmd.AddValue ("rate", "Data rate for p2p channel", nRate);     // in Mbps
    cmd.AddValue ("delay", "Delay for csma channel", nDelay);       // in ms
    cmd.Parse (argc, argv);

    GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
    GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
    LogComponentEnable("TapP2pVirtualMachine", LOG_INFO);

    // left 
    NodeContainer node_left;
    node_left.Create (2);
    CsmaHelper csmal;
    csmal.SetChannelAttribute("DataRate", StringValue("1Gbps"));
    csmal.SetChannelAttribute("Delay", StringValue(to_string(nDelay) + "ms"));
    NetDeviceContainer csma_left = csmal.Install (node_left);
    NS_LOG_INFO("CSMA left links created!");
    InternetStackHelper stackl;
    stackl.Install(node_left);
    Ipv4AddressHelper addrl;
    addrl.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifc_left = addrl.Assign(csma_left);

    TapBridgeHelper tabBridge(ifc_left.GetAddress(1));
    tabBridge.SetAttribute ("Mode", StringValue("UseBridge"));      // use a pre-created tap, and bridge to a bridging net device
    tabBridge.SetAttribute ("DeviceName", StringValue ("tap-left"));
    tabBridge.Install (node_left.Get (0), csma_left.Get (0));
    NS_LOG_INFO("Tab bridge left attrubutes set!");

    // right 
    NodeContainer node_right;
    node_right.Create (2);
    CsmaHelper csmar;
    csmar.SetChannelAttribute("DataRate", StringValue("1Gbps"));
    csmar.SetChannelAttribute("Delay", StringValue(to_string(nDelay) + "ms"));
    NetDeviceContainer csma_right = csmar.Install (node_right);
    NS_LOG_INFO("CSMA right links created!");
    InternetStackHelper stackr;
    stackr.Install(node_right);
    Ipv4AddressHelper addrr;
    addrr.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer ifc_right = addrr.Assign(csma_right);

    TapBridgeHelper tabBridge2(ifc_right.GetAddress(0));
    tabBridge2.SetAttribute ("Mode", StringValue("UseBridge")); 
    tabBridge2.SetAttribute ("DeviceName", StringValue ("tap-right"));
    tabBridge2.Install (node_right.Get (1), csma_right.Get (1));
    NS_LOG_INFO("Tab bridge right attrubutes set!");

    // p2p
    PointToPointHelper p2p;
    p2p.SetChannelAttribute("Delay", StringValue(to_string(nDelay) + "ms"));
    p2p.SetDeviceAttribute("DataRate", StringValue(to_string(nRate) + "Mbps"));
    NetDeviceContainer p2p_dev = p2p.Install(node_left.Get(1), node_right.Get(0));
    NS_LOG_INFO("Point to point link created!");    
    NS_LOG_INFO("Bottleneck set up: rate: " << nRate << " Mbps, delay: " << nDelay << " ms.");

    Ipv4AddressHelper addrp;
    addrp.SetBase ("10.1.2.0", "255.255.255.192");
    Ipv4InterfaceContainer ifc_p2p = addrp.Assign (p2p_dev);

    // log and tracing
    NS_LOG_INFO("\nIP address:\n" << "Left CSMA: " << ifc_left.GetAddress(0) << ", " << ifc_left.GetAddress(1));
    NS_LOG_INFO("P2p: " << ifc_p2p.GetAddress(0) << ", " << ifc_p2p.GetAddress(1));
    NS_LOG_INFO("Right CSMA: " << ifc_right.GetAddress(0) << ", " << ifc_right.GetAddress(1)); 

    csma_left.Get(1)->TraceConnectWithoutContext("MacRx", MakeCallback(&LeftReceived));
    csma_right.Get(0)->TraceConnectWithoutContext("MacRx", MakeCallback(&RightReceived));       //trace pkts from right side
    p2p.EnablePcapAll("TapCsma");

    // flow monitor
    // Ptr<FlowMonitor> flowmon;
    // FlowMonitorHelper flowmonHelper;
    // flowmon = flowmonHelper.InstallAll ();
    // flowmon->Start (Seconds (0.0));
    // flowmon->Stop (Seconds (tStop));

    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    Simulator::Stop (Seconds (tStop));
    Simulator::Run ();
    // flowmon->SerializeToXmlFile ("tap.flowmon", true, true);
    Simulator::Destroy ();

    return 0;
}