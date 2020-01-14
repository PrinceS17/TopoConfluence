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

#include "ns3/core-module.h"
#include "ns3/fnss-module.h"
#include "ns3/udp-echo-client.h"
#include "ns3/fnss-event.h"
#include "ns3/minibox-module.h"
#include "ns3/ratemonitor-module.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ppbp-application-module.h"

#include <string>
#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include <vector>

#define vint vector<uint32_t>
#define vdouble vector<double>

using namespace std;
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("BtnkAnalyzerTest");

enum FlowType {TARGET, CROSS};
string fname;
vector<string> fnames;
vector<vdouble> rates;


// find next node to dest: tool for finding path
Ptr<Node> findNext (Ptr<Node> cur, Ipv4Address destNetwork, FNSSSimulation sim)
{
	Ptr<Ipv4L3Protocol> ip = cur->GetObject<Ipv4L3Protocol> ();
	NS_ASSERT_MSG (ip, "Ipv4 is NULL!");
	Ptr<Ipv4RoutingProtocol> grp = ip->GetRoutingProtocol ();
	NS_ASSERT_MSG (grp, "Routing protocol is NULL!");
	Ptr<Ipv4GlobalRouting> gr = grp->GetObject<Ipv4GlobalRouting> ();
	NS_LOG_INFO (" -- Global routing: " << gr << "; # routes: " << gr->GetNRoutes ());
	if (!gr) return nullptr;
	
	Ipv4RoutingTableEntry* route;
	Ipv4Mask mask;
	for (uint32_t i = 0; i < gr->GetNRoutes (); i ++)
	{
		route= gr->GetRoute (i);
		// mask = route->GetDestNetworkMask ();
		mask = Ipv4Mask ("255.255.255.252");
        // NS_LOG_INFO (" -- Target: " << destNetwork << " Dest: " << route->GetDest () << " (" << route->GetDestNetwork () \
            << "): Mask " << mask << ", GW " << route->GetGateway ());
		if (route->GetDestNetwork ().GetSubnetDirectedBroadcast (mask) == destNetwork.GetSubnetDirectedBroadcast (mask) \
            || route->GetDestNetwork () == Ipv4Address ("0.0.0.0"))
		{
            NS_LOG_DEBUG (" -- Correct route found!");
			NS_LOG_INFO (" -- Target: " << destNetwork << " Dest: " << route->GetDest () << " (" << route->GetDestNetwork () \
                << "): Mask " << mask << ", GW " << route->GetGateway ());
			return sim.getNodeByIp(route->GetGateway ());
		}
	}
	return nullptr;
}


// find the path from txLeaf to rxLeaf
NodeContainer findPath (Ptr<Node> txLeaf, Ptr<Node> rxLeaf, FNSSSimulation sim, vector<string> nids, uint32_t i)
{
	Ptr<Ipv4L3Protocol> prot = rxLeaf->GetObject <Ipv4L3Protocol> ();
	Ipv4Address rxAddr = prot->GetInterface (i)->GetAddress (0).GetLocal ();
	NodeContainer path;
	
	path.Add (txLeaf);
    Ptr<Node> cur = txLeaf;
    do
    {
        cur = findNext (cur, rxAddr, sim);
        if (!cur) return NodeContainer ();
		path.Add (cur);
		NS_LOG_INFO ("Current node in path: " << cur->GetId ());
	} while (cur != rxLeaf);
	NS_LOG_DEBUG (" -> Found the path: size " << path.GetN ());
	stringstream ss;
	ss << "  - Path: ";
	for (uint32_t i = 0; i < path.GetN (); i ++)
	{
		ss << path.Get (i)->GetId ();
		if (i < path.GetN () - 1) ss << " -> ";
	}
	NS_LOG_DEBUG (ss.str ());

	return path;
}


// find the path to all the interfaces and choose the one with shortest length
NodeContainer findPath (Ptr<Node> txLeaf, Ptr<Node> rxLeaf, FNSSSimulation sim, vector<string> nids)
{
    Ptr<Ipv4L3Protocol> prot = rxLeaf->GetObject <Ipv4L3Protocol> ();
    NodeContainer finalPath;
    for (uint32_t i = 1; i < prot->GetNInterfaces (); i ++)
    {
        NodeContainer curPath = findPath (txLeaf, rxLeaf, sim, nids, i);
        NS_LOG_INFO (" >> found path to interface " << i << " of node " << rxLeaf->GetId () << ", size: "\
            << curPath.GetN () << ", " << prot->GetInterface (i)->GetAddress (0).GetLocal () << endl);
        if (!finalPath.GetN () || curPath.GetN () < finalPath.GetN ())
            finalPath = curPath;
    }
    return finalPath;
}


void fairShare (vector<vdouble>& Shares, vint& bnLabel, vdouble linkBw, uint32_t li)
{
    vdouble fRate;
    for (vdouble row : Shares) fRate.push_back (row [li]);
    double bw = linkBw[li];
    cout << "col: " << li << endl;

    // goal: allocate bw fairly to fRate
    double availBw = bw;
    double N = Shares.size ();
    double N_left = N;
    set<uint32_t> visited;

    // iterate to find all under-users
    bool any_underuser = true;
    while (any_underuser)
    {
        N_left = N - visited.size ();
        any_underuser = false;
        for (uint32_t i = 0; i < Shares.size (); i ++)
            if (fRate[i] < bw / N_left && visited.find (i) == visited.end ())
            {
                availBw -= fRate[i];
                visited.insert (i);
                any_underuser = true;
            }
        for (auto v : visited) cout << v << ", ";
    }
    cout << endl;

    for (uint32_t i = 0; i < Shares.size (); i ++)
    {
        if (visited.find (i) != visited.end ()) continue;
        Shares[i][li] = availBw / (N - (double) visited.size ());
        uint32_t bi = bnLabel[i];
        if (bi > Shares[i].size ()) bnLabel[i] = li;
        else if (Shares[i][li] <= Shares[i][bi] || !Shares[i][bi])
            bnLabel[i] = li;
    }

}


// parse the flow info file, store target flow, leaf BW and cross traffic
void parseFlowInfo (uint32_t nFlow, string infoFile, vector<vint>& targetFlow, vint& leafBw, vector<vint>& crossTraffic)
{
    ifstream fin (infoFile, ios::in);
    uint32_t src, des;
    double bw;
    vector<vint> tFlow, cTraffic;
    vint lBw, tmp;
    for (uint32_t i = 0; i < nFlow; i ++)
    {
        fin >> src >> des >> bw;
        tmp = {src, des};
        tFlow.push_back (tmp);
        lBw.push_back (bw);
    }
    while (!fin.eof ())
    {
        src = -1;
        des = -1;
        fin >> src >> des;
        if (src == 4294967295 || des == 4294967295) break;
        tmp = {src, des};
        cTraffic.push_back (tmp);
    }
    fin.close ();
    targetFlow = tFlow;
    leafBw = lBw;
    crossTraffic = cTraffic;
    NS_LOG_DEBUG (" - Flow info parsed, target flows:");
    for (uint32_t i = 0; i < nFlow; i ++)
        NS_LOG_DEBUG (" -- " << targetFlow[i][0] << " -> " << targetFlow[i][1] << ", " 
            << leafBw[i] << " bps");
    NS_LOG_DEBUG (" - Cross traffic, size of " << crossTraffic.size ());
    for (uint32_t i = 0; i < crossTraffic.size (); i ++)
        NS_LOG_DEBUG (" -- " << crossTraffic[i][0] << " -> " << crossTraffic[i][1]);
}


// generate one single flow given flow parameters
vector<Ipv4AddressHelper> generateFlow (FlowType type, Ptr<Node> tx, Ptr<Node> rx, Ipv4AddressHelper leftIp, Ipv4AddressHelper rightIp, 
    vdouble t, NodeContainer& end, NetDeviceContainer& txEndDev, NetDeviceContainer& rxEndDev, double dsBw = 0, double rate = 1000)
{
    Flow flow (tx, rx, leftIp, rightIp, (uint32_t) rate * 1e6, t);
    if (type == TARGET)
    {
        flow.build ("10Gbps", to_string(dsBw) + "bps");
        flow.setOnoff ();
    }
    else 
    {
        flow.build ();
        // flow.setOnoff ();
        flow.setPpbp ();
    }
    end.Add (flow.getHost (0));
    txEndDev.Add (flow.getEndDevice (0));
    rxEndDev.Add (flow.getEndDevice (1));

    vector<Ipv4AddressHelper> ips = {flow.getLeftAddr (), flow.getRightAddr ()};
    NS_LOG_INFO (" - n" << tx->GetId () << " -> n" << rx->GetId () << ": " << type << ", "
        << rate << " bps, ds: " << dsBw << " bps");
    return ips;
}


void printShares (vector<vdouble> S)
{
    stringstream ss;
    ss << "-- Shares: " << endl;
    for (auto row : S)
    {
        for (auto e : row)
        {
            ss << setprecision (2) << fixed << e << "  ";
        }
        ss << endl;
    }
    NS_LOG_DEBUG (ss.str ());
}

void printFlowRate (vdouble rate)
{
    stringstream ss;
    ss << "Rate for each flow: " << endl;
    for (auto r : rate)
        ss << r << " Mbps" << endl;
    NS_LOG_DEBUG (ss.str ());
}


int main (int argc, char *argv[])
{
    uint32_t mid;
    uint32_t nFlow;
    uint32_t mode = 0;
    double tStop = 60;
    string infoFile = "flow_info_btnktest.txt";
    string topoFile = "/home/sapphire/scpt/TopoSurfer/xml/Arpanet19706-5.0ms-bdp.xml";
    bool isCross = true;
    bool doGen = false;
    double tRate = 100;             // 100Mbps for target flows
    double cRate = 1000;            // 1Gbps for cross traffic

    CommandLine cmd;
    cmd.AddValue ("mid", "Run id", mid);
    cmd.AddValue ("nFlow", "Number of target flows", nFlow);
    cmd.AddValue ("mode", "Run mode: 0: separate, 1: integrated", mode);
    cmd.AddValue ("tStop", "Time to stop", tStop);
    cmd.AddValue ("flowInfo", "Flow info file generated by confluentSim.py", infoFile);
    cmd.AddValue ("topo", "xml topology file given by confluentSim.py", topoFile);
    cmd.AddValue ("cross", "If generate cross traffic in simulation", isCross);
    cmd.AddValue ("gen", "Generate the traffic or not", doGen);
    cmd.AddValue ("tRate", "Target flow rate (in Mbps)", tRate);
    cmd.AddValue ("cRate", "Cross traffic rate (in Mbps)", cRate);
    cmd.Parse (argc, argv);

    LogComponentEnable ("BtnkAnalyzerTest", LOG_LEVEL_INFO);
    LogComponentEnable ("FNSSSimulation", LOG_LEVEL_ALL);
    LogComponentEnable ("FNSSEvent", LOG_LEVEL_INFO);
    NS_LOG_DEBUG ("mid: " << mid << "\nnFlow: " << nFlow << "\nmode: " << mode << 
        "\ntStop: " << tStop << "\nflow info file: " << infoFile << "\nxml file: " << topoFile << "\n");

    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue (1400));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(4096 * 1024));      // 128 (KB) by default, allow at most 85Mbps for 12ms rtt
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(4096 * 1024));      // here we use 4096 KB
    
    // build topology
    FNSSSimulation sim (topoFile);
    sim.assignIPv4Addresses ("10.0.0.0");
    fnss::Topology topology = fnss::Parser::parseTopology (topoFile);
    set<string> nodeId = topology.getAllNodes ();
    vector<string> nids (nodeId.begin (), nodeId.end ());
    
    // parse info file
    NodeContainer txEnd, ctEnd;
    NetDeviceContainer txEndDev, rxEndDev, ctEndDev, crEndDev;
    Ipv4AddressHelper leftIp ("10.1.0.0", "255.255.255.0");
    Ipv4AddressHelper rightIp ("10.2.0.0", "255.255.255.0");
    vint leafBw;
    vector<vint> targetFlow, crossTraffic;       // leafBw: src -> leaf bw
    parseFlowInfo (nFlow, infoFile, targetFlow, leafBw, crossTraffic);
    uint32_t nCross = crossTraffic.size ();

    // ----------------------------------------------------------------
    // Ground Truth Calculation
    vdouble linkBw;             // BW of all links
    vdouble flowRate;           // desired rate for each flow
    vint bnLabel (nFlow + nCross, -1);
    vector<vdouble> Shares;     // share table: row: flow, column: link, entry: desire/fair share rate
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

    /* 
    Here we compute the ground truth of co-bottleneck based on all link capacity and routing
    path of each flow. The procedure is the following:
        1) iterate over all links, initiate vint linkBW by getting their bandwidth;
        2) initiate flowRates by max(100Mbps, leafBw[i]), cross traffic 1Gbps;
        3) for each flow: get path step by step, and fill in vector<vint> Shares (default 
            all 0s) with flowRates;
        4) for each link: compute fair share, update Shares;
        5) for each flow: set flowRate to min(Shares[flow]), record argmin(Shares[flow]) as 
            bottleneck, write Shares[flow] to flowRate;
        6) repeat 4) 5) until flowRate converges (not Shares because it's always varying),
            compare all flows' bottlenecks and write [flow:bottleneck] to file.
    */

    // 1) iterate over all links
    map<vint, uint32_t> end2link;           // map: (src, des) -> link index
    set<pair < string, string > > edges = topology.getAllEdges ();
    uint32_t i = 0;
    NS_LOG_INFO ("Process edges:");
    for (auto it = edges.begin (); it != edges.end (); it ++, i ++)
    {
        fnss::Edge edge = topology.getEdge (*it);
        vint end = vint {sim.getNode((*it).first)->GetId (), sim.getNode((*it).second)->GetId ()};
        sort (end.begin (), end.end ());
        end2link[end] = i;
        linkBw.push_back (edge.getCapacity ().getValue ());
        NS_LOG_INFO (" - [" << end[0] << ", " << end[1] << "] -> " << i << ", " << linkBw[i] << " Mbps");
        NS_ASSERT_MSG (edge.getCapacity ().getUnit () == "Mbps", "Invalid link capacity unit (should be Mbps)!");
    }
    for (auto p : end2link)
    {
        NS_LOG_DEBUG ("Test output: ends [" << p.first[0] << ", " << p.first[1] << "] -> " << "link " << p.second << ", " \
            << linkBw[p.second] << " Mbps");
    }

    // 2 & 3) initiate flowRates including cross traffic, iterate through flows & fill in Shares
    vector<vint> flows;
    flows.insert (flows.begin (), targetFlow.begin (), targetFlow.end ());
    flows.insert (flows.end (), crossTraffic.begin (), crossTraffic.end ());

    for (uint32_t i = 0; i < flows.size (); i ++)
    {
        double rate = i < nFlow? min (tRate, (double) leafBw[i] / 1e6) : cRate;
        flowRate.push_back (rate);
        
        Ptr<Node> txLeaf = sim.getNode (nids[flows[i][0]]);
        Ptr<Node> rxLeaf = sim.getNode (nids[flows[i][1]]);
        NodeContainer path = findPath (txLeaf, rxLeaf, sim, nids);

        stringstream ss;
        ss << "Final path of " << txLeaf->GetId () << " -> " << rxLeaf->GetId () << ": ";
        for (uint32_t k = 0; k < path.GetN (); k ++)
            ss << path.Get (k)->GetId () << ", ";
        NS_LOG_DEBUG (ss.str () << endl);

        Shares.push_back (vdouble (edges.size (), 0));
        for (uint32_t j = 0; j < path.GetN () - 1; j ++)        // iterate all the links along the path
        {
            vint end = vint {path.Get (j)->GetId (), path.Get (j + 1)->GetId ()};
            sort (end.begin (), end.end ());
            uint32_t li = end2link[end];
            Shares[i][li] = rate;
        }
    }
    printShares (Shares);

    // 4 ~ 6) for each link, compute the fair share and update Shares, fetch the bottleneck
    vdouble newRate = flowRate;
    do
    {
        for (uint32_t j = 0; j < edges.size (); j ++)
            fairShare (Shares, bnLabel, linkBw, j);
        NS_LOG_DEBUG ("Fair share: ");
        printShares (Shares);

        for (uint32_t i = 0; i < flows.size (); i ++)
        {
            vdouble row = Shares[i];
            int minx = -1;
            for (uint32_t k = 0; k < row.size (); k ++)
                if (minx < 0 && row[k] > 0) minx = k;
                else if (row[k] < row[minx] && row[k] > 0)
                    minx = k;
                
            newRate[i] = i < nFlow? min (row[minx], tRate) : min (row[minx], cRate);
            for (uint32_t j = 0; j < edges.size (); j ++)
                Shares[i][j] = Shares[i][j] > 0? newRate[i] : 0.0;
        }

        printFlowRate (newRate);
        printShares (Shares);

        if (newRate == flowRate) break;
        flowRate = newRate;                         // ! possible precision issue
    } while (true);

    // output & write to file
    stringstream ss;
    for (uint32_t i = 0; i < bnLabel.size (); i ++)
        ss << i << " " << bnLabel[i] << endl;
    NS_LOG_DEBUG ("Bottleneck result: \n" << ss.str ());
    
    string bnFile = "MboxStatistics/bottleneck_" + to_string(mid) + ".dat";
    ofstream bnOut (bnFile, ios::out);
    bnOut << ss.str ();
    bnOut.close ();

    // ----------------------------------------------------------------
    if (!doGen) return 0;

    // generate all traffic
    vdouble tconst = {0, tStop};
    vector<Ipv4AddressHelper> ips;
    for (uint32_t i = 0; i < nFlow; i ++)
    {
        uint32_t src = targetFlow[i][0];
        uint32_t des = targetFlow[i][1];
        NS_LOG_INFO (" --- src: " << src << ", des: " << des << ", size: " << nids.size ());

        Ptr<Node> tx = sim.getNode (nids[src]);
        Ptr<Node> rx = sim.getNode (nids[des]);
        ips = generateFlow (TARGET, tx, rx, leftIp, rightIp, tconst, txEnd, txEndDev, 
            rxEndDev, leafBw[i], tRate);
        leftIp = ips[0];
        rightIp = ips[1];
        NS_LOG_INFO (" -- " << src << " --> n" << tx->GetId () << ", " << des << " --> n" << rx->GetId ());
    }
    for (uint32_t i = 0; i < nCross; i ++)
    {
        if (!isCross) break;

        uint32_t src = crossTraffic[i][0];
        uint32_t des = crossTraffic[i][1];
        Ptr<Node> tx = sim.getNode (nids[src]);
        Ptr<Node> rx = sim.getNode (nids[des]);
        
        ips = generateFlow (CROSS, tx, rx, leftIp, rightIp, tconst, ctEnd, ctEndDev,
            crEndDev, cRate);
        leftIp = ips[0];
        rightIp = ips[1];
    }
    NS_LOG_DEBUG ("All flows generated.");

    Ipv4GlobalRoutingHelper::RecomputeRoutingTables ();
    Simulator::Stop (Seconds (tStop));
	Simulator::Run ();
	Simulator::Destroy ();

    return 0;
}