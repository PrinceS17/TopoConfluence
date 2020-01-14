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

/*
This program is used for simulation or emulation of BRITE generated topology. You can specify
related topology in TD_CustomWaxman.conf, e.g. AS number, number of nodes in each AS, and 
inter-AS and intra-AS bandwidth. Note that there're some errors (SIGSEGV) caused by insufficient
nodes in AS when the number of flows increases, so please increase it to fix such bug.

Specify either simulating or emulating the network by set useTap to 0/1 (1 for emulation).
Set the number of flows by set nFlow, edge CSMA rate by rate, etc. Please refer to the code for
more parameters.

Note that: in emulation, the IP of end hosts and tap names should be consistent with that of 
containers and taps in the real host. The IP generated here follows conventions below:
    Flow 1: 10.1.1.1 -> 10.1.1.2 -----> 10.2.1.1 -> 10.2.1.2        
    Flow n: 10.1.n.1 -> 10.1.n.2 -----> 10.2.n.1 -> 10.2.n.2
And container names and taps can be configured correspondingly using ./lxcInternet.sh and 
./buildMultiTapBridge.sh -i. You can also check the output to get flow info above and tap info.

*/

#include <string>
#include <iostream>
#include <fstream>
#include "ns3/minibox-module.h"
#include "ns3/mobility-module.h"
#include "ns3/brite-module.h"
#include "ns3/ipv4-nix-vector-helper.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("BriteMultTest");

// cook book: create node, add link, install stack, assign address, install application
// parameter for 1 flow: (edge delay,) rate, socket type, port


int main (int argc, char *argv[])
{    
    bool tracing = false;
    bool nix = false;
    bool useTap = false;                            // if use tap bridge for emulation
    uint32_t nFlow = 2;                             // 4 -> N = 16 in each AS
    uint32_t maxBytes = 0;                          // infinite stream
    double ncRate = 1000;                            // unit: Mbps
    double tStop = 60;
    string tap_left_prefix = "left", tap_right_prefix = "right";
    string confFile = "brite_conf/TD_CustomWaxman.conf";
    // string confFile = "src/brite/examples/conf_files/TD_ASBarabasi_RTWaxman.conf";   // topology from generic example
    string seedFile = "/home/sapphire/Documents/brite/last_seed_file";
    string newSeedFile = "/home/sapphire/Documents/brite/seed_file";

    CommandLine cmd;
    cmd.AddValue ("tracing", "Enable or disable ascii tracing", tracing);
    cmd.AddValue ("nix", "Enable or disable nix-vector routing", nix);
    cmd.AddValue ("useTap", "Whether use tap bridge", useTap);
    cmd.AddValue ("nFlow", "Number of flows", nFlow);
    cmd.AddValue ("maxBytes", "Total number of bytes to send (0 for infinite)", maxBytes);
    cmd.AddValue ("rate", "Rate for edge CSMA channel", ncRate);
    cmd.AddValue ("tStop", "Time to stop", tStop);
    cmd.AddValue ("left", "Left side taps' name prefix (TX)", tap_left_prefix);
    cmd.AddValue ("right", "Right side taps' name prefix (RX)", tap_right_prefix);
    cmd.AddValue ("confFile", "BRITE conf file", confFile);
    cmd.Parse (argc,argv);
    nix = false;
    string cRate = to_string(ncRate) + "Mbps";      // CSMA channel rate

    LogComponentEnable ("BriteMultTest", LOG_LEVEL_ALL);
    if (useTap)
    {
        // GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
        GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
    }

    // build a BRITE graph
    BriteTopologyHelper bth (confFile);
    bth.AssignStreams (7);
    InternetStackHelper stack;
    if (nix)
    {
        Ipv4NixVectorHelper nixRouting;
        stack.SetRoutingHelper (nixRouting);
    }
    Ipv4AddressHelper address ("10.0.0.0", "255.255.255.252");
    bth.BuildBriteTopology (stack);
    bth.AssignIpv4Addresses (address);
    NS_LOG_INFO ("Number of AS created " << bth.GetNAs ());

    // set up flows
    address.SetBase ("10.1.1.0", "255.255.255.0");
    Ipv4AddressHelper leftAddr ("10.1.1.0", "255.255.255.0");
    Ipv4AddressHelper rightAddr ("10.2.1.0", "255.255.255.0");

    for (uint32_t i = 0; i < nFlow; i ++)
    {
        NS_LOG_INFO ("Number of leaves: AS0: " << bth.GetNLeafNodesForAs (0) << "; AS1: " << bth.GetNLeafNodesForAs (1));
        Ptr<Node> txLeaf = bth.GetLeafNodeForAs (0, bth.GetNLeafNodesForAs (0) - 1 - i);
        Ptr<Node> rxLeaf = bth.GetLeafNodeForAs (1, bth.GetNLeafNodesForAs (1) - 1 - i);

        string tap_left = tap_left_prefix + "-" + to_string (i + 1);
        string tap_right = tap_right_prefix + "-" + to_string (i + 1);
        Flow normalFlow (txLeaf, rxLeaf, leftAddr, rightAddr, 10e6, {0, tStop}, maxBytes);
        normalFlow.build (cRate);
        if (useTap)
            normalFlow.setTapBridge (tap_left, tap_right);
        else normalFlow.setBulk ();
        
        leftAddr = normalFlow.getLeftAddr ();
        rightAddr = normalFlow.getRightAddr ();
    }

    if (!nix) Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    Simulator::Stop (Seconds (tStop));
    Simulator::Run ();
    Simulator::Destroy ();

    return 0;
}
