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
// This is an illustration of how one could use virtualization techniques to
// allow running applications on virtual machines talking over simulated
// networks.
//
// The actual steps required to configure the virtual machines can be rather
// involved, so we don't go into that here.  Please have a look at one of
// our HOWTOs on the nsnam wiki for more details about how to get the 
// system confgured.  For an example, have a look at "HOWTO Use Linux 
// Containers to set up virtual networks" which uses this code as an 
// example.
//
// The configuration you are after is explained in great detail in the 
// HOWTO, but looks like the following:
//
//  +----------+                                     +----------+
//  | virtual  |                                     | virtual  |
//  |  Linux   |                                     |  Linux   |
//  |   Host   |                                     |   Host   |
//  |          |                                     |          |
//  |   eth0   |                                     |   eth0   |
//  +----------+                                     +----------+
//       |                                                |
//  +----------+                                     +----------+
//  |  Linux   |                                     |  Linux   |
//  |  Bridge  |                                     |  Bridge  |
//  +----------+                                     +----------+
//       |                                                |
//  +------------+                                 +-------------+
//  | "tap-left" |                                 | "tap-right" |
//  +------------+                                 +-------------+
//       |           n0            n1                     |
//       |       +--------+    +--------+                 |
//       +-------|  tap   |    |  tap   |----  n3 --------+
//               | bridge |    | bridge |
//               +--------+    +--------+
//               |  CSMA  |    |  CSMA  |     CSMA
//               +--------+    +--------+
//                   |             |            |
//                   |             |            |
//                   |             |            |
//                   ============================
//                              CSMA LAN
//
#include <iostream>
#include <fstream>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/csma-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tap-bridge-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/tag.h"
#include "ns3/packet.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("CsmaLxcTest");

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
        fout.open(fname, ios::out | ios::trunc);
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
    double rate = 0;            // in kbps
    uint32_t pktSize = 1500;    // in byte
    bool isRun;
    fstream fout;
};

int 
main (int argc, char *argv[])
{
  double nRate = 10, nDelay = 2, period = 0.2;
  double tStop = 600.0;
  bool if_use_ip = false;
  bool if_mon = false;
  CommandLine cmd;
  cmd.AddValue ("rate", "Data rate for csma channel", nRate);       // in Mbps
  cmd.AddValue ("delay", "Delay for csma channel", nDelay);         // in ms
  cmd.AddValue ("T", "Detect period of rate monitor", period);      // in s
  cmd.AddValue ("ifIP", "If use IP setting", if_use_ip);            // 0/1
  cmd.AddValue ("ifMon", "If use ns-3 rate monitor", if_mon);
  cmd.Parse (argc, argv);

  RateMonitor rateMon(period);

  GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  LogComponentEnable("CsmaLxcTest", LOG_INFO);

  NS_LOG_INFO ("If use IP setting: " << if_use_ip << "; if monitor rate: " << if_mon);

  NodeContainer nodes;
  nodes.Create (2);

  //
  // Use a CsmaHelper to get a CSMA channel created, and the needed net 
  // devices installed on both of the nodes.  The data rate and delay for the
  // channel can be set through the command-line parser.  For example,
  //
  // ./waf --run "tap=csma-virtual-machine --ns3::CsmaChannel::DataRate=10000000"
  //
  CsmaHelper csma;
  string strRate = to_string(nRate) + "Mbps";
  string strDelay = to_string(nDelay) + "ms";
  csma.SetChannelAttribute("DataRate", StringValue(strRate));
  csma.SetChannelAttribute("Delay", StringValue(strDelay));
  NetDeviceContainer devices = csma.Install (nodes);
  NS_LOG_INFO("CSMA channel: bandwidth = " << strRate << ", delay = " << strDelay);

  // - Stack and IP assignment
//   if (if_use_ip)
//   {
//     InternetStackHelper ish;
//     ish.Install (nodes);
//     Ipv4AddressHelper ih;
//     ih.SetBase ("10.0.0.0", "255.255.255.0");
//     Ipv4InterfaceContainer ifc = ih.Assign (devices);
//   }

  //
  // Use the TapBridgeHelper to connect to the pre-configured tap devices for 
  // the left side.  We go with "UseBridge" mode since the CSMA devices support
  // promiscuous mode and can therefore make it appear that the bridge is 
  // extended into ns-3.  The install method essentially bridges the specified
  // tap to the specified CSMA device.
  //
  TapBridgeHelper tapBridge;
  tapBridge.SetAttribute ("Mode", StringValue ("UseBridge"));
  tapBridge.SetAttribute ("DeviceName", StringValue ("tap-left"));
  tapBridge.Install (nodes.Get (0), devices.Get (0));

  tapBridge.SetAttribute ("DeviceName", StringValue ("tap-right"));
  tapBridge.Install (nodes.Get (1), devices.Get (1));
  NS_LOG_INFO("Tap bridge installed.");

  // tracing
//   if (if_mon)
//   {
//     //   devices.Get(1)->TraceConnectWithoutContext("MacRx", MakeCallback(&RateMonitor::onMacRx, &rateMon));       // only tap bridge will not forward to MAC...
//     devices.Get(1)->TraceConnectWithoutContext("PhyRxEnd", MakeCallback(&RateMonitor::onMacRx, &rateMon));
//     rateMon.start();
//     Simulator::Schedule(Seconds(tStop), &RateMonitor::stop, &rateMon);
//   }

  csma.EnablePcapAll("lxc-test");

//   // - global routing
//   if (if_use_ip)
//     Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  Simulator::Stop (Seconds (tStop));
  Simulator::Run ();


  Simulator::Destroy ();
}
