/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/core-module.h"
#include "ns3/minibox-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ratemonitor.h"
#include "ns3/ratemonitor-helper.h"

using namespace ns3;


int 
main (int argc, char *argv[])
{
  bool verbose = true;
  uint32_t cRate = 10000000;    // 10 Mbps
  double tStop = 30;

  CommandLine cmd;
  cmd.AddValue ("verbose", "Tell application to log if true", verbose);
  cmd.AddValue ("cRate", "Client sending rate", cRate);
  cmd.AddValue ("tStop", "Time to stop", tStop);
  cmd.Parse (argc,argv);

  if (verbose) LogComponentEnable ("RateMonitor", LOG_LEVEL_INFO);

  // build topology
  NodeContainer nodes;
  nodes.Create (2);
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));
  NetDeviceContainer devices = p2p.Install (nodes);
  InternetStackHelper stack;
  stack.Install (nodes);
  Ipv4AddressHelper addr ("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer ifc = addr.Assign (devices);
  Ipv4AddressHelper left ("10.1.0.0", "255.255.255.0");
  Ipv4AddressHelper right ("10.2.0.0", "255.255.255.0");
  

  // add flow and monitor
  Flow flow (nodes.Get (0), nodes.Get (1), left, right, cRate, {0, tStop});
  flow.build ("1Gbps");
  flow.setOnoff ();

  vector<uint32_t> id = {123, 0};
  RateMonitor rmon (id);
  rmon.install (devices.Get (1));
  rmon.start (Seconds (0.1));
  rmon.stop (Seconds (tStop));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}


