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

// Network topology (manually build): left, right, down, east are 4 Linux Containers
// 
//                     CSMA                                        CSMA
//          left[0] ------------ left[1]               right[1] ------------ right[0]
//                   10.1.1.0       |                    |       10.1.3.0
//                                  | p2p                |  p2p 
//                        10.1.4.0  |       10.1.5.0     |  10.1.6.0
//                              middle[0] ---------- middle[1] 
//                                  |   p2p: bottleneck  |       
//                                  | p2p                |  p2p
//                                  | 10.1.8.0           |  10.1.9.0
//                     CSMA         |                    |         CSMA
//          down[0] ------------ down[1]               east[1] ------------ east[0]
//                   10.1.7.0                                    10.1.10.0

// host: sudo route add -net 10.1.3.0 gw 10.1.1.2 netmask 255.255.255.0 dev tap1
// container: route add -net 10.1.1.0 gw 10.1.3.1 netmask 255.255.255.0 dev eth0
    // 

#include <iostream>
#include <fstream>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/tap-bridge-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("LxcLxcMultp");

int 
main (int argc, char *argv[])
{
  std::string mode = "UseBridge";              // only for tap1
  std::string tapName1 = "tap-left", tapName2 = "tap-right", tapName3 = "tap-down", tapName4 = "tap-east";
  double tStop = 600;

  CommandLine cmd;
  cmd.AddValue ("mode", "Mode setting of TapBridge", mode);
  cmd.AddValue ("tStop", "Time of the simulation", tStop);
  cmd.AddValue ("tapName1", "Name of the OS tap device (left)", tapName1);
  cmd.AddValue ("tapName2", "Name of the OS tap device (right)", tapName2);
  cmd.AddValue ("tapName3", "Name of the OS tap device (lower)", tapName3);
  cmd.AddValue ("tapName4", "Name of the OS tap device (east)", tapName4);
  cmd.Parse (argc, argv);

  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  LogComponentEnable("LxcLxcMultp", LOG_INFO);

  CsmaHelper csma;
  csma.SetChannelAttribute("DataRate", StringValue("1Gbps"));
  // csma.SetChannelAttribute("Delay", StringValue("2ms"));
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("300Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  NS_LOG_INFO ("CSMA rate: 1Gbps, delay: 0ms");
  NS_LOG_INFO ("P2P rate: 300Mbps, delay: 1ms");

  //
  // The topology has a Wifi network of four nodes on the left side.  We'll make
  // node zero the AP and have the other three will be the STAs.
  //
  NodeContainer nodesLeft;
  nodesLeft.Create (2);
  NetDeviceContainer devicesLeft = csma.Install (nodesLeft);
  InternetStackHelper internetLeft;
  internetLeft.Install (nodesLeft);
  Ipv4AddressHelper ipv4Left;
  ipv4Left.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesLeft = ipv4Left.Assign (devicesLeft);

  TapBridgeHelper tapBridge (interfacesLeft.GetAddress (1));        // why this address? --> Doc: gateway address!!!
  tapBridge.SetAttribute ("Mode", StringValue (mode));
  tapBridge.SetAttribute ("DeviceName", StringValue (tapName1));
  tapBridge.Install (nodesLeft.Get (0), devicesLeft.Get (0));
  NS_LOG_INFO("Left tap gateway: " << interfacesLeft.GetAddress (1));

  // lower side of the nodes
  NodeContainer nodesDown;
  nodesDown.Create (2);
  NetDeviceContainer devicesDown = csma.Install (nodesDown);
  InternetStackHelper internetDown;
  internetDown.Install (nodesDown);
  Ipv4AddressHelper ipv4Down;
  ipv4Down.SetBase ("10.1.7.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesDown = ipv4Down.Assign (devicesDown);

  TapBridgeHelper tapBridgeDown (interfacesDown.GetAddress (1));
  tapBridgeDown.SetAttribute ("Mode", StringValue (mode));
  tapBridgeDown.SetAttribute ("DeviceName", StringValue (tapName3));
  tapBridgeDown.Install (nodesDown.Get (0), devicesDown.Get (0));
  NS_LOG_INFO ("Lower left tap gateway: " << interfacesDown.GetAddress (1));

  // right side
  NodeContainer nodesRight;
  nodesRight.Create (2);
  NetDeviceContainer devicesRight = csma.Install (nodesRight);
  InternetStackHelper internetRight;
  internetRight.Install (nodesRight);
  Ipv4AddressHelper ipv4Right;
  ipv4Right.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesRight = ipv4Right.Assign (devicesRight);

  // add a tap bridge to node 7
  TapBridgeHelper tapBridge2(interfacesRight.GetAddress(0));
  tapBridge2.SetAttribute ("Mode", StringValue ("UseBridge"));
  tapBridge2.SetAttribute ("DeviceName", StringValue (tapName2));
  tapBridge2.Install (nodesRight.Get (1), devicesRight.Get (1));         // install on node 5: 10.1.3.2
  NS_LOG_INFO("Right tap gateway: " << interfacesRight.GetAddress(0));

  // lower-right nodes: east
  NodeContainer nodesEast;
  nodesEast.Create (2);
  NetDeviceContainer devicesEast = csma.Install (nodesEast);
  InternetStackHelper internetEast;
  internetEast.Install (nodesEast);
  Ipv4AddressHelper ipv4East;
  ipv4East.SetBase ("10.1.10.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesEast = ipv4East.Assign (devicesEast);

  TapBridgeHelper tapBridgeEast (interfacesEast.GetAddress (0));
  tapBridgeEast.SetAttribute ("Mode", StringValue (mode));
  tapBridgeEast.SetAttribute ("DeviceName", StringValue (tapName4));
  tapBridgeEast.Install (nodesEast.Get (1), devicesEast.Get (1));
  NS_LOG_INFO ("Lower right tap gateway: " << interfacesEast.GetAddress (0));

  //
  // Stick in the point-to-point line between the sides.
  //
  NodeContainer middle;
  middle.Create (2);
  InternetStackHelper ish;
  ish.Install (middle);

  NetDeviceContainer dev1 = p2p.Install (nodesLeft.Get (1), middle.Get (0));
  NetDeviceContainer dev2 = p2p.Install (middle.Get (0), middle.Get (1));
  NetDeviceContainer dev3 = p2p.Install (middle.Get (1), nodesRight.Get (0));
  NetDeviceContainer dev4 = p2p.Install (nodesDown.Get (1), middle.Get (0));
  NetDeviceContainer dev5 = p2p.Install (middle.Get (1), nodesEast.Get (0));

  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.4.0", "255.255.255.192");
  ipv4.Assign (dev1);
  ipv4.NewNetwork ();
  ipv4.Assign (dev2);
  ipv4.NewNetwork ();
  ipv4.Assign (dev3);
  ipv4.SetBase ("10.1.8.0", "255.255.255.0");
  ipv4.Assign (dev4);
  ipv4.NewNetwork ();
  ipv4.Assign (dev5);

  p2p.EnablePcapAll ("lxc-multi");
  csma.EnablePcapAll ("lxc-lxc-csma", false);
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  Simulator::Stop (Seconds (tStop));
  Simulator::Run ();
  Simulator::Destroy ();
}
