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
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/packet-sink.h"
#include "ns3/brite-module.h"
#include "ns3/ipv4-nix-vector-helper.h"
#include <iostream>
#include <fstream>

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("BriteBulkTest");

int
main (int argc, char *argv[])
{
  // LogComponentEnable ("BulkSendApplication", LOG_INFO);
  // LogComponentEnable ("PacketSink", LOG_INFO);
  LogComponentEnable ("BriteBulkTest", LOG_LEVEL_ALL);

  // BRITE configure files
  string confFile = "brite_conf/TD_CustomWaxman.conf";
  // string confFile = "src/brite/examples/conf_files/TD_ASBarabasi_RTWaxman.conf";   // topology from generic example
  string seedFile = "/home/sapphire/Documents/brite/last_seed_file";
  string newSeedFile = "/home/sapphire/Documents/brite/seed_file";
  bool tracing = false;
  bool nix = false;
  uint32_t maxBytes = 100e6;
  double npRate = 100;
  double npDelay = 2;
  double tStop = 10;

  CommandLine cmd;
  cmd.AddValue ("maxBytes", "Total number of bytes to send (0 for infinite)", maxBytes);
  cmd.AddValue ("confFile", "BRITE conf file", confFile);
  cmd.AddValue ("tracing", "Enable or disable ascii tracing", tracing);
  cmd.AddValue ("nix", "Enable or disable nix-vector routing", nix);
  cmd.AddValue ("rate", "Rate for edge PPP link", npRate);
  cmd.AddValue ("delay", "Delay for edge PPP link", npDelay);
  cmd.AddValue ("tStop", "Time to stop", tStop);
  cmd.Parse (argc,argv);
  nix = false;
  string pRate = to_string (npRate) + "Mbps";
  string pDelay = to_string (npDelay) + "ms";

  // build a BRITE graph
  BriteTopologyHelper bth (confFile);
  bth.AssignStreams (3);
  InternetStackHelper stack;

  if (nix)
    {
      Ipv4NixVectorHelper nixRouting;
      stack.SetRoutingHelper (nixRouting);
    }

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.252");
  bth.BuildBriteTopology (stack);
  bth.AssignIpv4Addresses (address);
  NS_LOG_INFO ("Number of AS created " << bth.GetNAs ());


  // attach nodes
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue (pRate));
  p2p.SetChannelAttribute ("Delay", StringValue (pDelay));
  NS_LOG_INFO ("Edge link: rate: " << pRate << ", delay: " << pDelay);
  
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

  NetDeviceContainer p2pClientDevices, p2pServerDevices;
  p2pClientDevices = p2p.Install (client);
  p2pServerDevices = p2p.Install (server);

  // assign address
  address.SetBase ("10.1.0.0", "255.255.0.0");
  Ipv4InterfaceContainer clientInterfaces;
  clientInterfaces = address.Assign (p2pClientDevices);
  address.SetBase ("10.2.0.0", "255.255.0.0");
  Ipv4InterfaceContainer serverInterfaces;
  serverInterfaces = address.Assign (p2pServerDevices);
  NS_LOG_INFO ("Src: " << clientInterfaces.GetAddress (0) << ";  Gateway: " << clientInterfaces.GetAddress (1));
  NS_LOG_INFO ("Des: " << serverInterfaces.GetAddress (0) << ";  Gateway: " << serverInterfaces.GetAddress (1));

  // set bulk application for source client[0], send to server[0]
  uint16_t port = 9;
  BulkSendHelper source ("ns3::TcpSocketFactory", InetSocketAddress (serverInterfaces.GetAddress (0), port));
  source.SetAttribute ("MaxBytes", UintegerValue (maxBytes));
  ApplicationContainer sourceApps = source.Install (client.Get (0));
  sourceApps.Start (Seconds (0.0));
  sourceApps.Stop (Seconds (tStop));

  // set packet sink for destination: server[0]
  PacketSinkHelper sink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApps = sink.Install (server.Get (0));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (Seconds (tStop));
  NS_LOG_INFO ("Application set up.");

  if (!nix)
    {
      Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    }

  if (tracing)
    {
      AsciiTraceHelper ascii;
      p2p.EnableAsciiAll (ascii.CreateFileStream ("briteLeaves.tr"));
    }

  // Run the simulator
  Simulator::Stop (Seconds (tStop));
  Simulator::Run ();
  Simulator::Destroy ();

  // Ptr<PacketSink> sink1 = DynamicCast<PacketSink> (sinkApps.Get (0));
  // NS_LOG_INFO ("Total bytes received: " << sink1->GetTotalRx ());

  return 0;
}
