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

#include <vector>
#include <chrono>
#include <ctime>  
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/tools.h"

#define clock chrono::system_clock
#define timePoint chrono::system_clock::time_point


// Default Network Topology
//
//       10.1.1.0
// n0 -------------- n1   n2   n3   n4
//    point-to-point  |    |    |    |
//                    ================
//                      LAN 10.1.2.0

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("SecondScriptExample");


class runtimeAnalyzer
{
public:
  runtimeAnalyzer(uint32_t N = 3): N(N) 
  {
    timer = vector<double> (N, 0);
    button = vector<bool> (N, false);
  };

  double nowt ()
  {
    chrono::duration<double> nt = clock::now() - iniTime;
    return nt.count();
  }
  
  void tapRx (Ptr<const Packet> p)
  {
    NS_LOG_INFO (nowt() << " s: Tap Rx");
  }
  void tapTx (Ptr<const Packet> p)
  {
    NS_LOG_INFO (nowt() << " s: Tap Tx");
  }

  void p2pPhyRxEnd (Ptr<const Packet> p)
  {
    NS_LOG_INFO (nowt() << " s: P2P PhyRxEnd");
  }
  void p2pPhyTxEnd (Ptr<const Packet> p)
  {
    NS_LOG_INFO (nowt() << " s: P2P PhyTxEnd");
  }

  void ipRx (Ptr<const Packet> p, Ptr< Ipv4 > ip, uint32_t i)
  {
    NS_LOG_INFO (nowt() << " s: Ipv4 Rx");
  }
  void ipTx (Ptr<const Packet> p, Ptr< Ipv4 > ip, uint32_t i)
  {
    NS_LOG_INFO (nowt() << " s: Ipv4 Tx");
  }

  void tcpRx (Ptr<const Packet> p, const TcpHeader& th, Ptr<const TcpSocketBase> tsb)
  {
    NS_LOG_INFO (nowt() << " s: TCP Rx");
  }
  void tcpTx (Ptr<const Packet> p, const TcpHeader& th, Ptr<const TcpSocketBase> tsb)
  {
    NS_LOG_INFO (nowt() << " s: TCP Tx");
  }


  void connect()            // connect all trace sources
  {
    iniTime = clock::now();
    string devPrefix = "/NodeList/*/DeviceList/*/$ns3::";
    string nodePrefix = "/NodeList/*/$ns3::";
    Config::ConnectWithoutContext (devPrefix + "PointToPointNetDevice/PhyRxEnd", MakeCallback(&runtimeAnalyzer::p2pPhyRxEnd, this));
    Config::ConnectWithoutContext (devPrefix + "PointToPointNetDevice/PhyTxEnd", MakeCallback(&runtimeAnalyzer::p2pPhyTxEnd, this));
    Config::ConnectWithoutContext (nodePrefix + "Ipv4L3Protocol/Rx", MakeCallback(&runtimeAnalyzer::ipRx, this));
    Config::ConnectWithoutContext (nodePrefix + "Ipv4L3Protocol/Tx", MakeCallback(&runtimeAnalyzer::ipTx, this));
    Config::ConnectWithoutContext (nodePrefix + "TcpSocketBase/Rx", MakeCallback(&runtimeAnalyzer::tcpRx, this));
    Config::ConnectWithoutContext (nodePrefix + "TcpSocketBase/Tx", MakeCallback(&runtimeAnalyzer::tcpTx, this));
    Config::ConnectWithoutContext (devPrefix + "TapBridge/TapRx", MakeCallback(&runtimeAnalyzer::tapRx, this));
    Config::ConnectWithoutContext (devPrefix + "TapBridge/TapTx", MakeCallback(&runtimeAnalyzer::tapTx, this));
    
  }
  
private:
  uint32_t N;               // # component to record time
  vector<double> timer;
  timePoint iniTime;
  timePoint curTime;
  vector<bool> button;      // determine on/off of the timer
};


// trace sink for debug
void LastMacTx (Ptr<const Packet> p)
{
    NS_LOG_INFO ("-------------------- Last MacTx! -------------------------");
}

void LastMacRx (Ptr<const Packet> p)
{
    NS_LOG_INFO ("-------------------- Last MacRx! -------------------------");
}

int 
main (int argc, char *argv[])
{
  bool verbose = true;
  bool useTcp = false;
  double tStop = 5;
  uint32_t nCsma = 3;

  CommandLine cmd;
  cmd.AddValue ("nCsma", "Number of \"extra\" CSMA nodes/devices", nCsma);
  cmd.AddValue ("verbose", "Tell echo applications to log if true", verbose);
  cmd.AddValue ("useTcp", "If use TCP application to test (Onoff)", useTcp);
  cmd.AddValue ("tStop", "Stop time of the simulation", tStop);

  cmd.Parse (argc,argv);

  LogComponentEnable ("SecondScriptExample", LOG_INFO);
  if (verbose)
    {
      LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("UdpEchoServerApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("OnOffApplication", LOG_LEVEL_INFO);
      LogComponentEnable ("PacketSink", LOG_LEVEL_INFO);
    }

  nCsma = nCsma == 0 ? 1 : nCsma;

  NodeContainer p2pNodes;
  p2pNodes.Create (2);

  NodeContainer csmaNodes;
  csmaNodes.Add (p2pNodes.Get (1));
  csmaNodes.Create (nCsma);

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer p2pDevices;
  p2pDevices = pointToPoint.Install (p2pNodes);

  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", StringValue ("100Mbps"));
  csma.SetChannelAttribute ("Delay", TimeValue (NanoSeconds (6560)));

  NetDeviceContainer csmaDevices;
  csmaDevices = csma.Install (csmaNodes);

  InternetStackHelper stack;
  stack.Install (p2pNodes.Get (0));
  stack.Install (csmaNodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer p2pInterfaces;
  p2pInterfaces = address.Assign (p2pDevices);

  address.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer csmaInterfaces;
  csmaInterfaces = address.Assign (csmaDevices);

  if(useTcp)
  {
    uint32_t port =  5000;
    Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
    PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
    ApplicationContainer sinkApp = sinkHelper.Install (csmaNodes.Get (nCsma));
    sinkApp.Start (Seconds (1.0));
    sinkApp.Stop (Seconds (tStop));

    OnOffHelper client ("ns3::TcpSocketFactory", InetSocketAddress(csmaInterfaces.GetAddress (nCsma), port));
    client.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
    client.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
    ApplicationContainer apps = client.Install (p2pNodes.Get (0));
    apps.Start (Seconds (1.0));
    apps.Stop (Seconds (tStop));
  }
  else
  {
    UdpEchoServerHelper echoServer (9);
    ApplicationContainer serverApps = echoServer.Install (csmaNodes.Get (nCsma));
    serverApps.Start (Seconds (1.0));
    serverApps.Stop (Seconds (tStop));

    UdpEchoClientHelper echoClient (csmaInterfaces.GetAddress (nCsma), 9);
    echoClient.SetAttribute ("MaxPackets", UintegerValue (1));
    echoClient.SetAttribute ("Interval", TimeValue (Seconds (1.0)));
    echoClient.SetAttribute ("PacketSize", UintegerValue (1024));

    ApplicationContainer clientApps = echoClient.Install (p2pNodes.Get (0));
    clientApps.Start (Seconds (2.0));
    clientApps.Stop (Seconds (tStop));
  }

  // addition: connect test
  // Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/MacRx", MakeCallback (&LastMacRx));
  // p2pDevices.Get(0)->TraceConnectWithoutContext ("MacRx", MakeCallback(&LastMacRx));

  runtimeAnalyzer raz;
  raz.connect ();                   // test here

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  pointToPoint.EnablePcapAll ("second");
  csma.EnablePcap ("second", csmaDevices.Get (1), true);


  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
