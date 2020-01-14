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

#include <string>
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/packet-sink.h"
#include "ns3/brite-module.h"
#include "ns3/ipv4-nix-vector-helper.h"
#include "ns3/tap-bridge-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/minibox-module.h"
#include "ns3/ratemonitor-module.h"
#include "ns3/brite-module.h"
#include <iostream>
#include <fstream>

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("BriteEmuTest");

int
main (int argc, char *argv[])
{
  // LogComponentEnable ("BulkSendApplication", LOG_INFO);
  // LogComponentEnable ("PacketSink", LOG_INFO);
  LogComponentEnable ("BriteEmuTest", LOG_LEVEL_ALL);
  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  // BRITE configure files
  string confFile = "brite_conf/TD_CustomWaxman.conf";
  // string confFile = "src/brite/examples/conf_files/TD_ASBarabasi_RTWaxman.conf";   // topology from generic example
  string seedFile = "/home/sapphire/Documents/brite/last_seed_file";
  string newSeedFile = "/home/sapphire/Documents/brite/seed_file";
  bool tracing = false;
  bool nix = false;
  uint32_t maxBytes = 100e6;
  double ncRate = 200;
  double ncDelay = 0;
  double tStop = 600;

  CommandLine cmd;
  cmd.AddValue ("maxBytes", "Total number of bytes to send (0 for infinite)", maxBytes);
  cmd.AddValue ("confFile", "BRITE conf file", confFile);
  cmd.AddValue ("tracing", "Enable or disable ascii tracing", tracing);
  cmd.AddValue ("nix", "Enable or disable nix-vector routing", nix);
  cmd.AddValue ("rate", "Rate for edge PPP link", ncRate);
  cmd.AddValue ("delay", "Delay for edge PPP link", ncDelay);
  cmd.AddValue ("tStop", "Time to stop", tStop);
  cmd.Parse (argc,argv);
  nix = false;
  string cRate = to_string (ncRate) + "Mbps";
  string cDelay = to_string (ncDelay) + "ns";

  // build a BRITE graph
  BriteTopologyHelper bth (confFile);
  bth.AssignStreams (2);
  InternetStackHelper stack;

  if (nix)
    {
      Ipv4NixVectorHelper nixRouting;
      stack.SetRoutingHelper (nixRouting);
    }

  Ipv4AddressHelper address;
  address.SetBase ("10.2.0.0", "255.255.255.252");
  bth.BuildBriteTopology (stack);
  bth.AssignIpv4Addresses (address);
  NS_LOG_INFO ("Number of AS created " << bth.GetNAs ());

  // attach nodes
  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", StringValue (cRate));
  csma.SetChannelAttribute ("Delay", StringValue (cDelay));
  NS_LOG_INFO ("Edge link: rate: " << cRate << ", delay: " << cDelay);
  
  NodeContainer client, server;

  // install client node on last leaf node of AS 0
  client.Create (1);
  stack.Install (client);
  int numLeafNodesInAsZero = bth.GetNLeafNodesForAs (0);
  client.Add (bth.GetLeafNodeForAs (0, numLeafNodesInAsZero - 1));

  // install server node on last leaf node on AS 1
  server.Create (1);
  stack.Install (server);
  int numLeafNodesInAsOne = bth.GetNLeafNodesForAs (1);
  server.Add (bth.GetLeafNodeForAs (1, numLeafNodesInAsOne - 1));

  NetDeviceContainer clientDevices, serverDevices;
  clientDevices = csma.Install (client);
  NodeContainer serverRev(server.Get (1), server.Get (0));
  serverDevices = csma.Install (serverRev);     // make 10.1.3.2 the end node

  // add a cross traffic in ns-3 to test the slow time
  Ipv4AddressHelper leftAddr ("10.1.4.0", "255.255.255.0");
  Ipv4AddressHelper rightAddr ("10.1.5.0", "255.255.255.0");
  Ptr<Node> txLeaf = bth.GetLeafNodeForAs (0, 1);
  Ptr<Node> rxLeaf = bth.GetLeafNodeForAs (1, 1);
  Flow crossTraffic (txLeaf, rxLeaf, leftAddr, rightAddr, 10000000, {0, 3});
  crossTraffic.build (to_string (1000) + "Mbps");
  crossTraffic.setOnoff ();


  // assign address
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer clientInterfaces;
  clientInterfaces = address.Assign (clientDevices);
  address.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer serverInterfaces;
  serverInterfaces = address.Assign (serverDevices);
  NS_LOG_INFO ("Src: " << clientInterfaces.GetAddress (0) << ";  Gateway: " << clientInterfaces.GetAddress (1));
  NS_LOG_INFO ("Des: " << serverInterfaces.GetAddress (1) << ";  Gateway: " << serverInterfaces.GetAddress (0));

  // set bulk application for source client[0], send to server[0]
  // TapBridgeHelper tbh (clientInterfaces.GetAddress (1));
  TapBridgeHelper tbh;
  tbh.SetAttribute ("Mode", StringValue ("UseBridge"));
  tbh.SetAttribute ("DeviceName", StringValue ("tap-left"));
  tbh.Install (client.Get (0), clientDevices.Get (0));

  // tbh.SetAttribute ("Gateway", Ipv4AddressValue (serverInterfaces.GetAddress (0)));
  tbh.SetAttribute ("DeviceName", StringValue ("tap-right"));
  tbh.Install (serverRev.Get (1), serverDevices.Get (1));     // careful!
  NS_LOG_INFO ("Tap bridge set up for BRITE topology!");

  if (!nix)
    {
      Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    }

  if (tracing)
    {
      AsciiTraceHelper ascii;
      csma.EnableAsciiAll (ascii.CreateFileStream ("briteLeaves.tr"));
    }

  // Run the simulator
  Simulator::Stop (Seconds (tStop));
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
