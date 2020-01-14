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

// Network topology (2 VMs, 1 host -> google cloud)
//
//                     CSMA                                    
//          left[0] ------------ left[1]               
//                   10.1.1.0       |                  
//                                  | p2p leaf             
//                        10.1.2.0  |       10.1.5.0            10.1.6.0
//                              middle[0] ---------- middle[1] ---------- middle[2] (host)
//                                  |   p2p bottleneck            CSMA
//                                  | p2p leaf               
//                                  | 10.1.4.0           
//                     CSMA         |                    
//          right[0] ------------ right[1]               
//                   10.1.3.0                                  

// 

#include <iostream>
#include <fstream>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/fd-net-device-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/tap-bridge-module.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("VmVmHost");

int 
main (int argc, char *argv[])
{
    string tap1 = "tap-left", tap2 = "tap-right", tap3 = "tap-middle";
    uint32_t leafRate = 1000;
    uint32_t bottleneckRate = 100;
    uint32_t delay = 5;
    double tStop = 30;

    CommandLine cmd;
    cmd.AddValue ("tStop", "Time of the simulation (in s)", tStop);
    cmd.AddValue ("tap1", "Name of the OS tap device (left)", tap1);
    cmd.AddValue ("tap2", "Name of the OS tap device (right)", tap2);
    cmd.AddValue ("tap3", "Name of the OS tap device (host)", tap3);
    cmd.AddValue ("leafRate", "Rate of leaf p2p link (in Mbps)", leafRate);
    cmd.AddValue ("bRate", "Rate of bottleneck (in Mbps)", bottleneckRate);
    cmd.AddValue ("bDelay", "Delay of bottleneck link (in ms)", delay);
    cmd.Parse (argc, argv);

    GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
    GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
    LogComponentEnable ("VmVmHost", LOG_INFO);
    NS_LOG_INFO ("tap 1~3: " << tap1 << ", " << tap2 << ", " << tap3);

    string bRateStr = to_string (bottleneckRate) + "Mbps";
    string bDelayStr = to_string (delay) + "ms";
    CsmaHelper csma;
    csma.SetChannelAttribute ("DataRate", StringValue ("10Gbps"));
    // csma.SetChannelAttribute ("Delay", StringValue ("5ms"));     // just for test
    PointToPointHelper leaf, bottleneck;
    leaf.SetDeviceAttribute ("DataRate", StringValue (to_string (leafRate) + "Mbps"));
    leaf.SetChannelAttribute ("Delay", StringValue ("1ms"));
    bottleneck.SetDeviceAttribute ("DataRate", StringValue (bRateStr));
    bottleneck.SetChannelAttribute ("Delay", StringValue (bDelayStr));
    NS_LOG_INFO ("Leaf rate: " << leafRate << "Mbps");
    NS_LOG_INFO ("Bottleneck: rate: " + bRateStr + ", delay: " + bDelayStr + ".");

    // node, p2p, stack (order in each device: left/right -> middle)
    NodeContainer leftNode, rightNode, middleNode, temp;
    leftNode.Create (2);
    rightNode.Create (2);
    middleNode.Create (3);
    temp.Add (middleNode.Get (1));
    temp.Add (middleNode.Get (2));
    NetDeviceContainer leftCsma, rightCsma, leftPpp, rightPpp, middleCsma, middlePpp;
    leftCsma = csma.Install (leftNode);
    rightCsma = csma.Install (rightNode);
    leftPpp = leaf.Install (leftNode.Get (1), middleNode.Get (0));
    rightPpp = leaf.Install (rightNode.Get (1), middleNode.Get (0));
    middlePpp = bottleneck.Install (middleNode.Get (0), middleNode.Get (1));
    middleCsma = csma.Install (temp);
    InternetStackHelper stack;
    stack.InstallAll ();
    NS_LOG_INFO ("Node created, stack installed.");

    // IP assign
    Ipv4AddressHelper addr ("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifc;
    ifc.Add (addr.Assign (leftCsma));
    addr.NewNetwork ();
    ifc.Add (addr.Assign (leftPpp));
    addr.NewNetwork ();
    ifc.Add (addr.Assign (rightCsma));
    addr.NewNetwork ();
    ifc.Add (addr.Assign (rightPpp));
    addr.NewNetwork ();
    ifc.Add (addr.Assign (middlePpp));
    addr.NewNetwork ();
    ifc.Add (addr.Assign (middleCsma));
    vector<string> names = {"left", "right", "middle"};
    for (uint32_t i = 0; i < 3; i ++)
        NS_LOG_INFO (names[i] << ": " << ifc.GetAddress (4*i) << " -> " << ifc.GetAddress (4*i + 1) << ", " << ifc.GetAddress (4*i + 2) << " -> " << ifc.GetAddress (4*i + 3));

    // tap bridge
    TapBridgeHelper leftTap(ifc.GetAddress (1)), rightTap (ifc.GetAddress (5)), middleTap (ifc.GetAddress (10));
    leftTap.SetAttribute ("Mode", StringValue ("UseBridge"));
    leftTap.SetAttribute ("DeviceName", StringValue (tap1));
    leftTap.Install (leftNode.Get (0), leftCsma.Get (0));
    rightTap.SetAttribute ("Mode", StringValue ("UseBridge"));
    rightTap.SetAttribute ("DeviceName", StringValue (tap2));
    rightTap.Install (rightNode.Get (0), rightCsma.Get (0));
    middleTap.SetAttribute ("Mode", StringValue ("UseBridge"));
    middleTap.SetAttribute ("DeviceName", StringValue (tap3));
    middleTap.Install (middleNode.Get (2), middleCsma.Get (1));
    NS_LOG_INFO ("Tap bridge set up.");

    // trace
    leaf.EnablePcapAll ("vm-leaf");
    csma.EnablePcapAll ("vm-csma");
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    Simulator::Stop (Seconds (tStop));
    Simulator::Run ();
    Simulator::Destroy ();
}
