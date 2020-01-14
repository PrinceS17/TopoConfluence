/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/minibox.h"

using namespace std;
using namespace ns3;

int 
main (int argc, char *argv[])
{
  double tStop = 30;
  double nRate = 50;      // rate of a single p2p link, in Mbps
  double nDelay = 5;      // delay of the link, in ms
  uint32_t cRate = 100;    // client sending rate, in Mbps     

  CommandLine cmd;
  cmd.AddValue ("tStop", "Time to stop", tStop);
  cmd.AddValue ("rate", "Rate of p2p link", nRate);
  cmd.AddValue ("delay", "Delay of p2p link", nDelay);
  cmd.AddValue ("cRate", "Client sending rate", cRate);
  cmd.Parse (argc,argv);

  string rate = to_string (nRate) + "Mbps";
  string delay = to_string (nDelay) + "ms";
  uint32_t clientRate = cRate * 1000000;

  // general settings
  LogComponentEnable ("MiniBox", LOG_INFO);
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue (1400));   
  Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4096 * 1024));      // 128 (KB) by default, allow at most 85Mbps for 12ms rtt
  Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(4096 * 1024));      // here we use 4096 KB
  // Packet::EnablePrinting ();     // would cause isStateOk() error


  // build link, device, network
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue (rate));
  p2p.SetChannelAttribute ("Delay", StringValue (delay));   
  TrafficControlHelper red;
  red.SetRootQueueDisc("ns3::RedQueueDisc", 
                       "MinTh", DoubleValue(5),
                       "MaxTh", DoubleValue(15),
                       "QueueLimit", UintegerValue(120),
                       "LinkBandwidth", StringValue(rate),
                       "LinkDelay", StringValue(delay));

  NodeContainer nodes;
  nodes.Create (2);
  NetDeviceContainer devices = p2p.Install (nodes);
  InternetStackHelper stack;
  stack.Install (nodes);
  red.Install (devices.Get (0));

  Ipv4AddressHelper address ("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer interface = address.Assign (devices);

  Ipv4AddressHelper leftAddr ("10.1.0.0", "255.255.255.0");
  Ipv4AddressHelper rightAddr ("10.2.0.0", "255.255.255.0");
  NodeContainer hosts;
  NetDeviceContainer endDevices;
  uint32_t nFlow = 2;

  for (uint32_t i = 0; i < nFlow; i ++)
  {
    Flow flow (nodes.Get (0), nodes.Get (1), leftAddr, rightAddr, clientRate, {0, tStop});
    flow.build (to_string(clientRate) + "bps");
    flow.setOnoff ();

    leftAddr = flow.getLeftAddr ();           // important: update address to avoid repeatment
    rightAddr = flow.getRightAddr ();
    hosts.Add (flow.getHost (0));
    endDevices.Add (flow.getEndDevice (0));
  }

  // set up minibox
  MiniBox mnbox ({798, 0}), mnbox2 ({798, 1});
  vector< Ptr<MiniBox> > mnboxes;

  for (uint32_t i = 0; i < nFlow; i ++)       // example of create object for MiniBox, directly create may cause the vanishment of members
  {
    Ptr<MiniBox> mnbox = CreateObject < MiniBox, vector<uint32_t> > ({798, i});
    mnboxes.push_back (mnbox);
    mnbox->install (hosts.Get (i), endDevices.Get (i));
    mnbox->start (Seconds (0.001));
    mnbox->stop (Seconds (tStop));
  }

  // tracing and debug
  p2p.EnablePcapAll ("minibox");

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
  Simulator::Stop (Seconds (tStop));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
