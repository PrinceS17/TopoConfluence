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

// Network topology
//
//  +-------------+                           +-------------+
//  |   external  |                           |    Linux    |
//  |    Linux    |                           |  Container  |
//  |     Host    |                           |             |
//  |             |                           |             |
//  |  "tap-left" |                           | "tap-right" |
//  +-------------+                           +-------------+
//       |           n1              n2              |
//       |       +--------+     +---------+          |
//       +-------|  tap   |     |   tap   |----------+
//               | bridge |     |  bridge |
//               +--------+     +---------+
//               |  CSMA  |-----|   CSMA  |                          
//               +--------+     +---------+                      

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

NS_LOG_COMPONENT_DEFINE ("HostLxcSimplest");

int 
main (int argc, char *argv[])
{
  std::string mode = "ConfigureLocal";              // only for tap1
  std::string tapName1 = "tap-left", tapName2 = "tap-right";
  double tStop = 600;

  CommandLine cmd;
  cmd.AddValue ("mode", "Mode setting of TapBridge", mode);
  cmd.AddValue ("tStop", "Time of the simulation", tStop);
  cmd.AddValue ("tapName1", "Name of the OS tap device (left)", tapName1);
  cmd.AddValue ("tapName2", "Name of the OS tap device (right)", tapName2);
  cmd.Parse (argc, argv);

  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  LogComponentEnable("HostLxcSimplest", LOG_INFO);

  NodeContainer nodes;
  NetDeviceContainer dev;
  nodes.Create (3);
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("100Gbps"));
  dev.Add (p2p.Install (nodes.Get (0), nodes.Get (1)));

  CsmaHelper csma;  
  csma.SetChannelAttribute ("DataRate", StringValue ("100Gbps"));
  NodeContainer csma_nodes(nodes.Get (1), nodes.Get (2));
  dev.Add (csma.Install (csma_nodes));

  InternetStackHelper stack;
  stack.Install (nodes);
  Ipv4AddressHelper addr;
  addr.SetBase ("10.0.0.0", "255.255.255.0");
  addr.Assign (dev);
  
  TapBridgeHelper tbh;
  tbh.SetAttribute ("Mode", StringValue (mode));
  tbh.SetAttribute ("DeviceName", StringValue (tapName1));
  tbh.Install (nodes.Get (0), dev.Get (0));

  tbh.SetAttribute ("Mode", StringValue ("UseBridge"));
  tbh.SetAttribute ("DeviceName", StringValue (tapName2));
  tbh.Install (nodes.Get (2), dev.Get (2));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  Simulator::Stop (Seconds (tStop));
  Simulator::Run ();
  Simulator::Destroy ();

}
