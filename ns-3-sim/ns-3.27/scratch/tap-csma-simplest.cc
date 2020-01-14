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
//  +----------+                           +----------+
//  |  Linux   |                           |  Linux   |
//  |Container |                           |Container |
//  |          |                           |          |
//  |   eth0   |                           |   eth0   |
//  +----------+                           +----------+
//       |                                      |
//  +----------+                           +----------+
//  |  Linux   |                           |  Linux   |
//  |  Bridge  |                           |  Bridge  |
//  +----------+                           +----------+
//       |                                      |
//  +------------+                       +-------------+
//  | "tap-left" |                       | "tap-right" |
//  +------------+                       +-------------+
//       |           n0            n1           |
//       |       +--------+    +--------+       |
//       +-------|  tap   |    |  tap   |-------+
//               | bridge |    | bridge |
//               +--------+    +--------+
//               |  CSMA  |    |  CSMA  |
//               +--------+    +--------+
//                   |  CSMA LAN   |
//                   ===============
//
#include <iostream>
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mpi-interface.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/tap-bridge-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-global-routing-helper.h"

#ifdef NS3_MPI
#include <mpi.h>
#endif

using namespace ns3;
using namespace std;
NS_LOG_COMPONENT_DEFINE ("TapCsmaSimplest");

int 
main (int argc, char *argv[])
{
  string mode = "UseBridge";
  bool nullmsg = false;
  CommandLine cmd;
  cmd.AddValue ("mode", "Mode of tap bridge", mode);
  cmd.AddValue ("nullmsg", "Enable the use of null-message synchronization", nullmsg);
  cmd.Parse (argc, argv);

  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  // if(nullmsg) GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::NullMessageSimulatorImpl"));
  // else GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::DistributedSimulatorImpl"));
  // MpiInterface::Enable (&argc, &argv);
  // uint32_t systemId = MpiInterface::GetSystemId ();
  // uint32_t systemCount = MpiInterface::GetSize ();

  LogComponentEnable ("TapCsmaSimplest", LOG_INFO);
  // NS_LOG_INFO ("System count: " << systemCount);

  // NodeContainer left, right;    // create left & right node with system id 0 & 1
  // left.Create (1, 0);
  // right.Create (1, 1); 
  // NodeContainer nodes(left, right);
  NodeContainer nodes;
  nodes.Create (2);

  CsmaHelper csma;
  csma.SetChannelAttribute("DataRate", StringValue("100Gbps"));
  csma.SetChannelAttribute("Delay", StringValue ("0ns"));
  NetDeviceContainer devices = csma.Install (nodes);

  InternetStackHelper stack;
  stack.Install (nodes);
  Ipv4AddressHelper addr;
  addr.SetBase("10.0.0.0", "255.255.255.0");
  addr.Assign (devices);

  TapBridgeHelper tapBridge;
  tapBridge.SetAttribute ("Mode", StringValue (mode));

  // if (systemId == 0)
  // {
    tapBridge.SetAttribute ("DeviceName", StringValue ("tap-left"));
    tapBridge.Install (nodes.Get (0), devices.Get (0));
  // }
  // else
  // {
    tapBridge.SetAttribute ("DeviceName", StringValue ("tap-right"));
    tapBridge.Install (nodes.Get (1), devices.Get (1));
  // }

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  Simulator::Stop (Seconds (600.));
  Simulator::Run ();
  Simulator::Destroy ();
  // MpiInterface::Disable ();
  return 0;
}
