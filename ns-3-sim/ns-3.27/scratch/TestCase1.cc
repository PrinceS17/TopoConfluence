#include <iostream>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <iosfwd>
#include <sstream>
#include <math.h>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/tap-bridge-module.h"
#include "ns3/netanim-module.h"
#include "ns3/mobility-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/flow-monitor-module.h"
#include "sys/ioctl.h"

typedef std::chrono::duration<int, std::ratio_multiply<std::chrono::hours::period, std::ratio<24>>::type> TimestampDays;
using namespace ns3;

void printTime(){
    std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    TimestampDays days = std::chrono::duration_cast<TimestampDays>(duration);
    duration -= days;
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    duration -= hours;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    duration -= minutes;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    duration -= seconds;
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    std::cout << "\r" << "Simulation Time: " << Simulator::Now().GetSeconds() << "\tReal Time [" << hours.count() << ":" << minutes.count() << ":" << seconds.count() << "." << microseconds.count() << " UTC] ";
    std::cout.flush();
}


int main( int argc, char *argv[]){
    CommandLine cmd;
    cmd.Parse (argc, argv);
    NodeContainer nodes;
    ns3::Packet::EnablePrinting();
    GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
    GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
    
Simulator::Stop (Seconds (10.));
nodes.Create(1);

MobilityHelper mobility;
MobilityHelper waymobility;
Ptr<WaypointMobilityModel> mob;
waymobility.SetMobilityModel("ns3::WaypointMobilityModel");
Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
    
positionAlloc->Add (Vector (0,0,0));
mobility.SetPositionAllocator (positionAlloc);
mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
mobility.Install (nodes.Get(0));
AnimationInterface anim ("animation.xml");
anim.EnablePacketMetadata();
anim.UpdateNodeDescription(0,"node1");
for(uint64_t i = 0; i <= 10; i += 10){Simulator::Schedule(Seconds(i), &printTime);}
	Simulator::Run ();
	Simulator::Destroy ();
}
