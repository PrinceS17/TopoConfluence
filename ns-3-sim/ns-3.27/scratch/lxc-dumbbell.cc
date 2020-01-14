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
//  +----------+                                                  +----------+
//  | virtual  |                                                  | virtual  |
//  |  Linux   |                                                  |  Linux   |
//  |   Host   |                                                  |   Host   |
//  |          |                                                  |          |
//  |   eth0   |                                                  |   eth0   |
//  +----------+                                                  +----------+
//       |                                                             |
//  +----------+                                                  +----------+
//  |  Linux   |                                                  |  Linux   |
//  |  Bridge  |                                                  |  Bridge  |
//  +----------+                                                  +----------+
//       |                                                             |
//  +--------------+                                              +---------------+
//  | "tap-left-x" |                                              | "tap-right-x" |
//  +--------------+                                              +---------------+
//       |          s0-sn                                r0-rn         |
//       |       10.1.1.1                             10.1.3.2
//       |       +--------+                           +--------+       |
//       +-------|  tap   |                           |  tap   |-------+
//               | bridge |                           | bridge |
//               +--------+                           +--------+
//               |  CSMA  |                           |  CSMA  |
//               +--------+                           +--------+
//                   |                                    |
//                   |                                    |
//                   ========  n1 ------------- n2  =======
//                     CSMA         dumbell          CSMA
//
//
//              TEST NOT PASSED YET!

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
#include "ns3/point-to-point-dumbbell.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("LxcDumbbell");

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
    int nRate = 10, nDelay = 5, N = 3;
    double tStop = 600.0;
    string tap_left = "", tap_right = "";
    CommandLine cmd;
    cmd.AddValue ("N", "Number of flows", N);
    cmd.AddValue ("rate", "Data rate for p2p channel", nRate);     // in Mbps
    cmd.AddValue ("delay", "Delay for csma channel", nDelay);       // in ms
    cmd.AddValue ("tapLeft", "Prefix of the left tap names", tap_left);
    cmd.AddValue ("tapRight", "Prefix of the right tap names", tap_right);
    cmd.Parse (argc, argv);

    GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
    GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
    LogComponentEnable("LxcDumbbell", LOG_INFO);

    NS_ASSERT_MSG (tap_left.size() > 0 && tap_right.size() > 0, "Left & right tap name prefix must be specified!");

    // print all the parameters for double check
    stringstream ss;
    ss << "N: " << N << "; Bandwidth: " << nRate << " Mbps; " << "\nDelay (single link): " << nDelay << " ms;" << endl;
    ss << "Tap prefix: left: " << tap_left << "; right: " << tap_right << endl;
    NS_LOG_INFO (ss.str());

    // create nodes and helpers
    NodeContainer csma_left, csma_right;
    csma_left.Create (N);
    csma_right.Create (N);
    CsmaHelper csma;
    csma.SetChannelAttribute ("DataRate", StringValue("1Gbps"));
    csma.SetChannelAttribute ("Delay", StringValue("2ms"));
    
    PointToPointHelper p2p_bn, p2p_leaf;
    p2p_bn.SetDeviceAttribute ("DataRate", StringValue(to_string(nRate) + "Mbps"));
    p2p_bn.SetChannelAttribute ("Delay", StringValue(to_string(nDelay) + "ms"));
    p2p_leaf.SetDeviceAttribute ("DataRate", StringValue("800Mbps"));
    p2p_leaf.SetChannelAttribute ("Delay", StringValue("2ms"));

    PointToPointDumbbellHelper dh(N, p2p_leaf, N, p2p_leaf, p2p_bn);
    NS_LOG_INFO ("Node of " << N << " flows created, dumbbell created.");

    // set up the links, stacks, and addresses
    NetDeviceContainer csma_dev_left, csma_dev_right;
    InternetStackHelper stack_left, stack_right, stack;
    stack_left.Install (csma_left);
    stack_right.Install (csma_right);
    dh.InstallStack (stack);

    Ipv4AddressHelper ip_left, ip_right, ip_dhleft, ip_dhright, ip_bn;
    Ipv4InterfaceContainer ifc_left, ifc_right;
    ip_left.SetBase ("10.1.1.0", "255.255.255.0");
    ip_right.SetBase ("10.5.1.0", "255.255.255.0");
    ip_dhleft.SetBase ("10.2.1.0", "255.255.255.0");
    ip_dhright.SetBase ("10.4.1.0", "255.255.255.0");
    ip_bn.SetBase ("10.3.1.0", "255.255.255.0");

    for (uint32_t i = 0; i < N; i ++)
    {
        NodeContainer nc_left(csma_left.Get (i), dh.GetLeft (i));
        NetDeviceContainer dev_left = csma.Install (nc_left);
        csma_dev_left.Add (dev_left);
        NodeContainer nc_right (dh.GetRight (i), csma_right.Get (i));
        NetDeviceContainer dev_right = csma.Install (nc_right);
        csma_dev_right.Add (dev_right);

        ifc_left.Add (ip_left.Assign (dev_left));
        ip_left.NewNetwork ();
        ifc_right.Add (ip_right.Assign (dev_right));
        ip_right.NewNetwork ();
        NS_LOG_INFO (" - Left: " << ifc_left.GetAddress (2*i) << " -> " << ifc_left.GetAddress (2*i + 1) << 
            "; Right: " << ifc_right.GetAddress (2*i) << " -> " << ifc_right.GetAddress (2*i + 1));
    }
    dh.AssignIpv4Addresses (ip_dhleft, ip_dhright, ip_bn);
    NS_LOG_INFO ("Stack installed, addresses assigned.");

    // configure gateway and tapbridge
    for (uint32_t i = 0; i < N; i ++)
    {
        TapBridgeHelper tb_left (ifc_left.GetAddress (2*i + 1));        // set gateway to leaf of dumbbell
        tb_left.SetAttribute ("Mode", StringValue("UseBridge"));
        tb_left.SetAttribute ("DeviceName", StringValue(tap_left + "-" + to_string(i + 1)));
        tb_left.Install (csma_left.Get (i), csma_dev_left.Get (2*i));
        TapBridgeHelper tb_right (ifc_right.GetAddress (2*i));
        tb_right.SetAttribute ("Mode", StringValue("UseBridge"));
        tb_right.SetAttribute ("DeviceName", StringValue(tap_right + "-" + to_string(i + 1)));
        tb_right.Install (csma_right.Get (i), csma_dev_right.Get (2*i + 1));
        NS_LOG_INFO ("Gateway: left " << ifc_left.GetAddress (2*i + 1) << "; right " << ifc_right.GetAddress (2*i));
    }
    NS_LOG_INFO ("Tap bridge configured for each pair of left and right link.");

    // tracing
    csma.EnablePcapAll ("csma_dev");
    p2p_leaf.EnablePcapAll ("p2p_leaf");
    p2p_bn.EnablePcapAll ("p2p_bn");

    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    Simulator::Stop (Seconds (tStop));
    Simulator::Run ();
    // flowmon->SerializeToXmlFile ("tap.flowmon", true, true);
    Simulator::Destroy ();

    return 0;
}