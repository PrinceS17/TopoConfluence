/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/core-module.h"
#include "ns3/PPBP-helper.h"
#include "ns3/PPBP-application.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/ratemonitor-module.h"
#include "ns3/minibox-module.h"


#include <string>
#include <iostream>

using namespace std;
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("PPBPApplicationExample");

int 
main (int argc, char *argv[])
{
  bool verbose = true;
  double tStop = 90;
  bool isTcp = false;

  CommandLine cmd;
  cmd.AddValue ("verbose", "Tell application to log if true", verbose);
  cmd.AddValue ("tStop", "Time to stop", tStop);
  cmd.AddValue ("tcp", "If use TCP flow (0/1)", isTcp);
  cmd.Parse (argc,argv);
  LogComponentEnable ("PPBPApplicationExample", LOG_LEVEL_INFO);
  // LogComponentEnable ("PPBPApplication", LOG_LEVEL_ALL);
  string protocol = isTcp? "ns3::TcpSocketFactory" : "ns3::UdpSocketFactory";
  NS_LOG_INFO (protocol << " is in use.");

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

  // uint32_t port = 90;
  // PPBPHelper ph (protocol, InetSocketAddress (interface.GetAddress (1), port));
  // ph.SetAttribute ("MeanBurstArrivals", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  // ph.SetAttribute ("MeanBurstTimeLength", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
  // ApplicationContainer source = ph.Install (nodes.Get (0));
  // source.Start (Seconds (0));
  // source.Stop (Seconds (tStop));
  
  // PacketSinkHelper psh (protocol, InetSocketAddress (Ipv4Address::GetAny (), port));
  // ApplicationContainer sink = psh.Install (nodes.Get (1));
  // sink.Start (Seconds (0.0));
  // sink.Stop (Seconds (tStop));
  // NS_LOG_INFO ("Application started.");

  Ipv4AddressHelper leftIp ("10.1.0.0", "255.255.255.0");
  Ipv4AddressHelper rightIp ("10.2.0.0", "255.255.255.0");
  uint32_t rate = 5000000;

  Flow ppbpFlow (nodes.Get (0), nodes.Get (1), leftIp, rightIp, rate, {0, tStop}, \
    10000000, 90, protocol);
  ppbpFlow.build ();
  ppbpFlow.setPpbp (40, 0.1);
  Ptr<NetDevice> rxEndDev = ppbpFlow.getEndDevice (3);

  Ptr<RateMonitor> mon = CreateObject <RateMonitor, vector<uint32_t>> ({3333, 0});
  mon->install (rxEndDev);
  mon->start (Seconds (0.01));
  mon->stop (Seconds (tStop));
  NS_LOG_INFO ("Rate monitor installed.");

  p2p.EnablePcapAll ("PPBP");

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  Simulator::Stop (Seconds (tStop));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}


