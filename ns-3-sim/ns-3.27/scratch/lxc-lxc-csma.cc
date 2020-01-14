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
//  +----------+
//  | external |
//  |  Linux   |
//  |   Host   |
//  |          |
//  | "mytap"  |
//  +----------+
//       |           n0               n4
//       |       +--------+     +------------+
//       +-------|  tap   |     |            |
//               | bridge |     |            |
//               +--------+     +------------+
//               |  Wifi  |-----| P2P | CSMA |                          
//               +--------+     +-----+------+                      
//                   |       ^           |                          
//                 ((*))     |           |                          
//                        P2P 10.1.2     |                         
//                 ((*))                 |    n5 ------------ "tap2", Linux container, 10.1.3.2      
//                   |                   |     | 
//                  n1                   ========
//                     Wifi 10.1.1                CSMA LAN 10.1.3
//
// The CSMA device on node zero is:  10.1.1.1
// The CSMA device on node one is:   10.1.1.2
// The P2P device on node three is:  10.1.2.1
// The P2P device on node four is:   10.1.2.2
// The CSMA device on node four is:  10.1.3.1
// The CSMA device on node five is:  10.1.3.2
//
// Some simple things to do:
//
// 1) Ping one of the simulated nodes on the left side of the topology.
//
//    ./waf --run tap-wifi-dumbbell&
//    ping 10.1.1.3
//
// 2) Configure a route in the linux host and ping once of the nodes on the 
//    right, across the point-to-point link.  You will see relatively large
//    delays due to CBR background traffic on the point-to-point (see next
//    item).
//
//    ./waf --run tap-wifi-dumbbell&
//    sudo route add -net 10.1.3.0 netmask 255.255.255.0 dev thetap gw 10.1.1.2
//    ping 10.1.3.4
//
//    Take a look at the pcap traces and note that the timing reflects the 
//    addition of the significant delay and low bandwidth configured on the 
//    point-to-point link along with the high traffic.
//
// 3) Fiddle with the background CBR traffic across the point-to-point 
//    link and watch the ping timing change.  The OnOffApplication "DataRate"
//    attribute defaults to 500kb/s and the "PacketSize" Attribute defaults
//    to 512.  The point-to-point "DataRate" is set to 512kb/s in the script,
//    so in the default case, the link is pretty full.  This should be 
//    reflected in large delays seen by ping.  You can crank down the CBR 
//    traffic data rate and watch the ping timing change dramatically.
//
//    ./waf --run "tap-wifi-dumbbell --ns3::OnOffApplication::DataRate=100kb/s"&
//    sudo route add -net 10.1.3.0 netmask 255.255.255.0 dev thetap gw 10.1.1.2
//    ping 10.1.3.4
//
// 4) Try to run this in UseBridge mode.  This allows you to bridge an ns-3
//    simulation to an existing pre-configured bridge.  This uses tap devices
//    just for illustration, you can create your own bridge if you want.
//
//    sudo tunctl -t mytap1
//    sudo ifconfig mytap1 0.0.0.0 promisc up
//    sudo tunctl -t mytap2
//    sudo ifconfig mytap2 0.0.0.0 promisc up
//    sudo brctl addbr mybridge
//    sudo brctl addif mybridge mytap1
//    sudo brctl addif mybridge mytap2
//    sudo ifconfig mybridge 10.1.1.5 netmask 255.255.255.0 up
//    ./waf --run "tap-wifi-dumbbell --mode=UseBridge --tapName=mytap2"&
//    ping 10.1.1.3

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
#include "ns3/traffic-control-module.h"
#include "ns3/tools.h"

#include <chrono>
#define clock chrono::system_clock
#define timePoint chrono::system_clock::time_point

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("LxcLxcCsma");


class runtimeAnalyzer
{
public:
  runtimeAnalyzer(uint32_t N = 3): N(N) 
  {
    timer = vector<double> (N, 0);
    button = vector<bool> (N, false);
    lastPoint = vector<double> (N, 0);
    counter = vector<uint32_t> (N, 0);
  }

  double nowt ()
  {
    chrono::duration<double> nt = clock::now() - iniTime;
    return nt.count();
  }
  
  void tapRx (Ptr<const Packet> p)
  {
    double nt = nowt();
    NS_LOG_INFO (nt << " s: Tap Rx");
    if (timer[0] == 0)
    {
      timer[0] = nt;
      lastPoint[0] = nt;
    }
    else
    {
      double dur = nt - lastPoint[0];
      lastPoint[0] = nt;
      timer[0] += dur;

    }
    counter[0] ++;
  }
  void tapTx (Ptr<const Packet> p)
  {
    double nt = nowt();
    NS_LOG_INFO (nt << " s: Tap Tx");

    if (timer[1] == 0)
    {
      timer[1] = nt;
      lastPoint[1] = nt;
    }
    else
    {
      double dur = nt - lastPoint[1];
      lastPoint[1] = nt;
      timer[1] += dur;

    }
    counter[1] ++;
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
  void aggregate()          // show aggregate results
  {
    // NS_LOG_DEBUG ("Average TapRx interval: " << timer[0] / (double)counter[0]);
    // NS_LOG_DEBUG ("Average TapTx interval: " << timer[1] / (double)counter[1]);
    NS_LOG_DEBUG (nowt() << " " << timer[0] / (double)counter[0] << " " << timer[1] / (double)counter[1]);
    
    Simulator::Schedule(Seconds(0.2), &runtimeAnalyzer::aggregate, this);
  }
  
private:
  uint32_t N;               // # statistic to record time
  vector<uint32_t> counter;
  vector<double> lastPoint;
  vector<double> timer;
  timePoint iniTime;
  timePoint curTime;
  vector<bool> button;      // determine on/off of the timer
};

class RateMonitor
{
public:
    RateMonitor (double period = 0.2)
    {
        this->period = period;
        isRun = false;
        srand(time(0));
        id = rand() % 10000;
        string fname = "MboxStatistics/DataRate_" + to_string(id) + "_0.dat";
        string qname = "MboxStatistics/QueueSize_" + to_string(id) + "_0.dat";
        fout.open(fname, ios::out | ios::trunc);
        qout.open(qname, ios::out | ios::trunc);
        NS_LOG_INFO("ID: " << id);
    }
    void monitor ()
    {
        if(isRun)
            Simulator::Schedule(Seconds(period), &RateMonitor::monitor, this);
        rate = (double)recvWnd * pktSize * 8 / period / 1000;   // convert to kbps
        NS_LOG_INFO(Simulator::Now().GetSeconds() << "s. id = " << id << ":        " << rate << " kbps");
        fout << Simulator::Now().GetSeconds() << " " << rate << " kbps" << endl;
        recvWnd = 0;
        dropWnd = 0;
    }
    void onMacRx (Ptr<const Packet> p)
    {
        NS_LOG_FUNCTION(recvWnd);
        recvWnd ++;
        if(pktSize != p->GetSize() && p->GetSize() >= 1000)
        {
            pktSize = p->GetSize();
            NS_LOG_INFO("   - pkt size = " << pktSize);
        }
    }
    void onDrop (Ptr<const Packet> p)
    {
        NS_LOG_FUNCTION(dropWnd);
        NS_LOG_INFO ("Drop: " << dropWnd);

    }
    void pktInQ (uint32_t vOld, uint32_t vNew)
    {
        NS_LOG_FUNCTION(vNew);
        qout << Simulator::Now().GetSeconds() << " " << vNew << endl;
    }
    void start()
    {
        isRun = true;
        NS_LOG_INFO("Monitor start: T = " << period << ", pkt size = " << pktSize);
        monitor();
    }
    void stop()
    {
        isRun = false;
        fout.close();
        qout.close();
    }
    double getRate()
    {
        return rate;
    }
    uint32_t getId()
    {
        return id;
    }

private:
    uint32_t id;
    double period;        // in s
    uint32_t recvWnd = 0;
    uint32_t dropWnd = 0;
    double rate = 0;            // in kbps
    uint32_t pktSize = 1500;    // in byte
    bool isRun;
    fstream fout;
    fstream qout;
};

// trace sink for debug
void LastMacTx (Ptr<const Packet> p)
{
    NS_LOG_DEBUG ("-------------------- Last MacTx! -------------------------");
}

void LastMacRx (Ptr<const Packet> p)
{
    NS_LOG_DEBUG ("-------------------- Last MacRx! -------------------------");
}

int 
main (int argc, char *argv[])
{
  std::string mode = "UseBridge";              // only for tap1
  std::string tapName1 = "tap-left", tapName2 = "tap-right";
  double tStop = 600, period = 0.2, nRate = 2000, nDelay = 1;
  bool if_mon = false;
  bool if_p2p = false;
  bool log_time = false;

  CommandLine cmd;
  cmd.AddValue ("mode", "Mode setting of TapBridge", mode);
  cmd.AddValue ("tStop", "Time of the simulation", tStop);
  cmd.AddValue ("tapName1", "Name of the OS tap device (left)", tapName1);
  cmd.AddValue ("tapName2", "Name of the OS tap device (right)", tapName2);
  cmd.AddValue ("rate", "Numbeer of bottleneck bandwidth in Mbps", nRate);
  cmd.AddValue ("delay", "Delay of p2p (bottleneck link)", nDelay);
  cmd.AddValue ("T", "Detect period of rate monitor", period);    // in s
  cmd.AddValue ("ifMon", "If monitor the data rate", if_mon);
  cmd.AddValue ("ifP2P", "If use p2p from tap bridge to gateway", if_p2p);
  cmd.AddValue ("logTime", "If log runtime", log_time);
  cmd.Parse (argc, argv);

  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  LogComponentEnable("LxcLxcCsma", LOG_DEBUG);

  RateMonitor rateMon(period);
  runtimeAnalyzer raz;
  string pRate = to_string(nRate) + "Mbps";
  string pDelay = to_string(nDelay) + "ms";
  NS_LOG_INFO ("P2P rate: " << pRate << "; delay: " << pDelay << "; if monitor: " << if_mon);

  //
  // The topology has a Wifi network of four nodes on the left side.  We'll make
  // node zero the AP and have the other three will be the STAs.
  //
  NodeContainer nodesLeft;
  nodesLeft.Create (2);

  CsmaHelper csma;
  csma.SetChannelAttribute("DataRate", StringValue("1000Mbps"));
  csma.SetChannelAttribute("Delay", StringValue("2ns"));
  // NS_LOG_INFO("CSMA rate: 1Gbps, delay: -");

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("2Gbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("1ms"));
  Address macLeft = Address(Mac48Address("ba:24:eb:ee:ff:af"));
  Address macRight = Address(Mac48Address("7a:0f:7d:92:05:e2"));


  NetDeviceContainer devicesLeft;
  if(if_p2p)
  {
      devicesLeft = p2p.Install (nodesLeft);
      Ptr<PointToPointNetDevice> pl = DynamicCast<PointToPointNetDevice> (devicesLeft.Get (1));
      Ptr<PointToPointNetDevice> pl0 = DynamicCast<PointToPointNetDevice> (devicesLeft.Get (0));
      pl->useArp = true;
      pl0->promiscDestination = macLeft;
  }
  else devicesLeft = csma.Install (nodesLeft);

  InternetStackHelper internetLeft;
  internetLeft.Install (nodesLeft);

  TrafficControlHelper tch;
  tch.SetRootQueueDisc("ns3::RedQueueDisc", 
                      "MinTh", DoubleValue(5),
                      "MaxTh", DoubleValue(15),
                      "QueueLimit", UintegerValue(25),
                      "LinkBandwidth", StringValue(pRate),
                      "LinkDelay", StringValue(pDelay));
  QueueDiscContainer qc = tch.Install (devicesLeft.Get (1));

  Ipv4AddressHelper ipv4Left;
  ipv4Left.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesLeft = ipv4Left.Assign (devicesLeft);

  TapBridgeHelper tapBridge (interfacesLeft.GetAddress (1));        // why this address? --> Doc: gateway address!!!
  tapBridge.SetAttribute ("Mode", StringValue (mode));
  tapBridge.SetAttribute ("DeviceName", StringValue (tapName1));
  tapBridge.Install (nodesLeft.Get (0), devicesLeft.Get (0));
  NS_LOG_INFO("Left tap gateway: " << interfacesLeft.GetAddress (1));

  //
  // Now, create the right side.
  //
  NodeContainer nodesRight;
  nodesRight.Create (2);
  NetDeviceContainer devicesRight;
  if(if_p2p) 
  {
    devicesRight = p2p.Install (nodesRight);
    Ptr<PointToPointNetDevice> pr = DynamicCast<PointToPointNetDevice> (devicesRight.Get (0));
    Ptr<PointToPointNetDevice> pr1 = DynamicCast<PointToPointNetDevice> (devicesRight.Get (1));
    pr->useArp = true;
    pr1->promiscDestination = macRight;
    cout << macRight << endl;
  }
  else devicesRight = csma.Install (nodesRight);


  InternetStackHelper internetRight;
  internetRight.Install (nodesRight);

  Ipv4AddressHelper ipv4Right;
  ipv4Right.SetBase ("10.1.3.0", "255.255.255.0");
  // ipv4Right.SetBase ("10.5.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesRight = ipv4Right.Assign (devicesRight);

  // add a tap bridge to node 7
  TapBridgeHelper tapBridge2(interfacesRight.GetAddress(0));
  tapBridge2.SetAttribute ("Mode", StringValue (mode));
  tapBridge2.SetAttribute ("DeviceName", StringValue (tapName2));
  tapBridge2.Install (nodesRight.Get (1), devicesRight.Get (1));         // install on node 5: 10.1.3.2
  NS_LOG_INFO("Right tap gateway: " << interfacesRight.GetAddress(0));

  //
  // Stick in the point-to-point line between the sides.
  //
  PointToPointHelper p2p1;
  p2p1.SetDeviceAttribute ("DataRate", StringValue (pRate));
  p2p1.SetChannelAttribute ("Delay", StringValue (pDelay));

  NodeContainer nodes = NodeContainer (nodesLeft.Get (1), nodesRight.Get (0));
  NetDeviceContainer devices = p2p1.Install (nodes);

  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.2.0", "255.255.255.192");
  Ipv4InterfaceContainer interfaces = ipv4.Assign (devices);

  // set the ARP table manually: with arp-l3-protocol patched (FindCache: private -> public)
  //
  // ------------------ subject to change! if changed, directly break the link!!! --------------------
  //
  if(if_p2p)
  {
    //   Address macRight = Address (Mac48Address("00:00:00:00:00:04"));
    Ptr<Ipv4> ip1 = interfacesLeft.Get (1).first;
    Ptr<Ipv4> ip2 = interfacesRight.Get (0).first;
    Ptr<Ipv4Interface> ifaceLeft = ip1->GetObject<Ipv4L3Protocol> ()->GetInterface (interfacesLeft.Get (1).second);
    Ptr<Ipv4Interface> ifaceRight = ip2->GetObject<Ipv4L3Protocol> ()->GetInterface (interfacesRight.Get (0).second);

    addArpEntry (nodesLeft.Get (1), devicesLeft.Get (1), ifaceLeft, interfacesLeft.GetAddress (0), macLeft);
    addArpEntry (nodesRight.Get (0), devicesRight.Get (0), ifaceRight, interfacesRight.GetAddress (1), macRight);
  }

  stringstream ss;
  ss << "Mac address list: " << endl;
  ss << "Left gateway: " << devicesLeft.Get (1)->GetAddress () << " (" << devicesLeft.Get (1)->GetAddress().GetLength() << ")" << endl;
  ss << "Right gateway: " << devicesRight.Get (0)->GetAddress () << " (" << devicesRight.Get (0)->GetAddress().GetLength() << ")" << endl;
  cout << ss.str();

  // tracing and monitor
  // devicesRight.Get(1)->TraceConnectWithoutContext ("PhyRxEnd", MakeCallback(&RateMonitor::onMacRx, &rateMon));
  devicesLeft.Get (0)->TraceConnectWithoutContext ("MacTx", MakeCallback(&RateMonitor::onMacRx, &rateMon));    // test the sending rate
  devicesLeft.Get (0)->TraceConnectWithoutContext ("MacTxDrop", MakeCallback(&RateMonitor::onDrop, &rateMon));
  qc.Get (0) -> TraceConnectWithoutContext ("PacketsInQueue", MakeCallback(&RateMonitor::pktInQ, &rateMon));
  if (if_mon)
  {
    rateMon.start();
    Simulator::Schedule(Seconds(tStop), &RateMonitor::stop, &rateMon);
  }
  if (log_time) 
  {
    raz.connect ();
    raz.aggregate ();
  }

  p2p.EnablePcapAll ("lxc-lxc");
//   csma.EnablePcapAll ("lxc-lxc-csma", false);
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  Simulator::Stop (Seconds (tStop));
  Simulator::Run ();
  Simulator::Destroy ();
}
