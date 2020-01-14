/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/core-module.h"
#include "ns3/PPBP-helper.h"
#include "ns3/PPBP-application.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"

#include <string>
#include <iostream>

using namespace std;
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PPBPApplicationExample");

int 
main (int argc, char *argv[])
{
  bool verbose = true;
  double tStop = 250;

  CommandLine cmd;
  cmd.AddValue ("verbose", "Tell application to log if true", verbose);
  cmd.AddValue ("tStop", "Time to stop", tStop);
  cmd.Parse (argc,argv);
  LogComponentEnable ("PPBPApplicationExample", LOG_LEVEL_INFO);
  // LogComponentEnable ("PPBPApplication", LOG_LEVEL_ALL);
  

  NodeContainer nodes;
  nodes.Create (2);
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));
  NetDeviceContainer devices = p2p.Install (nodes);
  InternetStackHelper stack;
  stack.Install (nodes);
  Ipv4AddressHelper address ("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer interface = address.Assign (devices);
  NS_LOG_INFO ("Link built: " << interface.GetAddress (0) << " -> " << interface.GetAddress (1));

  uint32_t port = 90;
  PPBPHelper ph ("ns3::UdpSocketFactory", InetSocketAddress (interface.GetAddress (1), port));
  ApplicationContainer source = ph.Install (nodes.Get (0));
  source.Start (Seconds (0));
  source.Stop (Seconds (tStop));
  
  PacketSinkHelper psh ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sink = psh.Install (nodes.Get (1));
  sink.Start (Seconds (0.0));
  sink.Stop (Seconds (tStop));
  NS_LOG_INFO ("Application started.");

  p2p.EnablePcapAll ("PPBP");
  Simulator::Stop (Seconds (tStop));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}


