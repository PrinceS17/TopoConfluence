#ifndef TOOLS_H
#define TOOLS_H

#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

#include <vector>
#include <chrono>
#include "ns3/tag.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"

using namespace std;
using namespace ns3;

enum BeState {BEON, WARN, BEOFF};                 // State of soft control: Best-Effort on, Warn, Best-Effort off
enum ProtocolType {TCP, UDP};
enum FairType {NATURAL, PERSENDER, PRIORITY};     // Fairness type

bool is_debug = false;

// tool function, basically for parsing the packet header

// add ARP entry manually to avoid ARP request and reply at the beginning for ppp in emulation
void addArpEntry (Ptr<Node> node, Ptr<NetDevice> device,  Ptr<Ipv4Interface> interface, Ipv4Address ipv4, Address mac)
{
  Ptr<ArpL3Protocol> arp = node->GetObject<ArpL3Protocol> ();
  Ptr<ArpCache> cache;
  // try
  // {
  //   cache = arp->FindCache (device);
  // }
  // catch (...)                                      // won't work, since NS3ASSERT isn't active exception
  // {
    // cache = arp->CreateCache (device, interface);     // p2p will not have a exiting cache, somehow
  // }

  cache = interface->GetArpCache ();
  ArpCache::Entry *entry = cache->Add (ipv4);
  entry->SetMacAddress (mac);

}

vector<int> getPktSizes(Ptr <const Packet> p, ProtocolType pt)     // get [p2p size, ip size, tcp size, data size]
{
  // debug 
  if(is_debug) cout << " Begin get pkt sizes. " << endl;
  Ptr<Packet> pktCopy = p->Copy();
  vector<int> res;
  PppHeader pppH;
  Ipv4Header ipH;
  TcpHeader tcpH;
  UdpHeader udpH;

  res.push_back((int)pktCopy->GetSize());
  pktCopy->RemoveHeader(pppH);
  res.push_back((int)pktCopy->GetSize());
  pktCopy->RemoveHeader(ipH);
  res.push_back((int)pktCopy->GetSize());
  if(pt == TCP) 
  {
    pktCopy->RemoveHeader(tcpH);
  }
  else pktCopy->RemoveHeader(udpH);
  res.push_back((int)pktCopy->GetSize());
  return res;
}

vector<int> getPktSizesInDrop(Ptr <const Packet> p, ProtocolType pt)     // get p2p size, ip size, tcp size, data size]
{
  // debug 
  if(is_debug) cout << " Begin get pkt sizes in drop. " << endl;
  Ptr<Packet> pktCopy = p->Copy();
  vector<int> res;
  Ipv4Header ipH;
  TcpHeader tcpH;
  UdpHeader udpH;

  res.push_back((int)pktCopy->GetSize());
  // pktCopy->RemoveHeader(pppH);
  res.push_back((int)pktCopy->GetSize());
  pktCopy->RemoveHeader(ipH);
  res.push_back((int)pktCopy->GetSize());
  if(pt == TCP) 
  {
    pktCopy->RemoveHeader(tcpH);
  }
  else pktCopy->RemoveHeader(udpH);
  res.push_back((int)pktCopy->GetSize());
  return res;
}

vector<int> getPktSizesInQueue(Ptr <const Packet> p, ProtocolType pt)     // get [p2p size, ip size, tcp size, data size]
{
  // debug 
  if(is_debug) cout << " Begin get pkt sizes in queue . " << endl;
  Ptr<Packet> pktCopy = p->Copy();
  vector<int> res;
  // Ipv4Header ipH;
  TcpHeader tcpH;
  UdpHeader udpH;

  res.push_back((int)pktCopy->GetSize());
  // pktCopy->RemoveHeader(pppH);
  res.push_back((int)pktCopy->GetSize());
  // pktCopy->RemoveHeader(ipH);
  res.push_back((int)pktCopy->GetSize());
  if(pt == TCP) 
  {
    pktCopy->RemoveHeader(tcpH);
  }
  else pktCopy->RemoveHeader(udpH);
  res.push_back((int)pktCopy->GetSize());
  return res;
}

Ipv4Address getIpDesAddr(Ptr<const Packet> p)   // ip layer information, work for both TCP and UDP
{
  // debug 
  if(is_debug) cout << " Begin get ip des addr. " << endl;
  Ptr<Packet> pcp = p->Copy();
  PppHeader pppH;
  Ipv4Header ipH;
  pcp->RemoveHeader(pppH);
  pcp->RemoveHeader(ipH);
  return ipH.GetDestination();
}

Ipv4Address getIpSrcAddr(Ptr<const Packet> p)   // ip layer information, work for both TCP and UDP
{
  // debug 
  if(is_debug)  cout << " Begin get ip src addr." << endl;
  Ptr<Packet> pcp = p->Copy();
  
  if(pcp->GetSize() <= 1412)
    return 0;

  PppHeader pppH;
  Ipv4Header ipH;
  pcp->RemoveHeader(pppH);
  pcp->RemoveHeader(ipH);
  return ipH.GetSource();
}

uint32_t getTcpSize(Ptr <const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin get tcp size. " << endl;
  Ptr<Packet> pktCopy = p->Copy();
  PppHeader pppH;
  Ipv4Header ipH;
  TcpHeader tcpH;
  pktCopy->RemoveHeader(pppH);
  pktCopy->RemoveHeader(ipH);
  pktCopy->RemoveHeader(tcpH);
  return pktCopy->GetSize();
}

uint16_t getTcpFlag(Ptr <const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin get tcp flag. " << endl;
  Ptr<Packet> pktCopy = p->Copy();
  PppHeader pppH;
  Ipv4Header ipH;
  TcpHeader tcpH;
  pktCopy->RemoveHeader(pppH);
  pktCopy->RemoveHeader(ipH);
  pktCopy->RemoveHeader(tcpH);
  return (uint16_t)tcpH.GetFlags();
}

uint32_t getTcpSequenceNo(Ptr <const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin get tcp seq. " << endl;
  Ptr<Packet> pktCopy = p->Copy();

  PppHeader pppH;
  Ipv4Header ipH;
  TcpHeader tcpH;

  pktCopy->RemoveHeader(pppH);
  pktCopy->RemoveHeader(ipH);
  pktCopy->PeekHeader(tcpH);
  return tcpH.GetSequenceNumber().GetValue();
}

uint32_t getTcpSequenceNoInDrop(Ptr <const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin get tcp seq in drop. " << endl;
  Ptr<Packet> pktCopy = p->Copy();
  Ipv4Header ipH;
  TcpHeader tcpH;

  pktCopy->RemoveHeader(ipH);
  pktCopy->PeekHeader(tcpH);
  return tcpH.GetSequenceNumber().GetValue();
}

uint32_t getTcpSequenceNoInQueue(Ptr <const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin get tcp seq in queue. " << endl;
  Ptr<Packet> pktCopy = p->Copy();
  TcpHeader tcpH;
  pktCopy->PeekHeader(tcpH);
  return tcpH.GetSequenceNumber().GetValue();
}

uint32_t getTcpAckNo(Ptr <const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin get tcp ack no. " << endl;
  Ptr<Packet> pktCopy = p->Copy();

  PppHeader pppH;
  Ipv4Header ipH;
  TcpHeader tcpH;

  pktCopy->RemoveHeader(pppH);
  pktCopy->RemoveHeader(ipH);
  pktCopy->PeekHeader(tcpH);
  return tcpH.GetAckNumber().GetValue();
}

uint16_t getTcpWin(Ptr <const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin get tcp win. " << endl;
  Ptr<Packet> pktCopy = p->Copy();
  PppHeader pppH;
  Ipv4Header ipH;
  TcpHeader tcpH;

  pktCopy->RemoveHeader(pppH);
  pktCopy->RemoveHeader(ipH);
  pktCopy->RemoveHeader(tcpH);
  return tcpH.GetWindowSize();

}

string 
printPkt(Ptr <const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin print pkt. " << endl;
  Ptr<Packet> pktCopy = p->Copy();
  stringstream ss;

  pktCopy->Print(ss);
  return ss.str();
}

string
printTcpPkt(Ptr <const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin print tcp pkt. " << endl;
  Ptr<Packet> pcp = p->Copy();
  stringstream ss;
  PppHeader pppH;
  Ipv4Header ipH;

  pcp->RemoveHeader(pppH);
  pcp->RemoveHeader(ipH);
  pcp->Print(ss);
  return ss.str();
}

string
logIpv4Header (Ptr<const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin log header. " << endl;
  Ptr<Packet> pktCopy = p->Copy ();
  PppHeader pppH;
  Ipv4Header ipH;
  pktCopy->RemoveHeader (pppH);
  pktCopy->PeekHeader (ipH); /// need to know the exact structure of header
  stringstream ss;
  ipH.Print (ss);
  return ss.str ();
}

string
logPppHeader (Ptr<const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin log header. " << endl;
  Ptr<Packet> pktCopy = p->Copy ();
  PppHeader pppH;
  pktCopy->PeekHeader (pppH);
  stringstream ss;
  pppH.Print (ss);
  return ss.str ();
}

string
logTcpHeader (Ptr<const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin log header. " << endl;
  Ptr<Packet> pktCopy = p->Copy ();
  PppHeader pppH;
  Ipv4Header ipH;
  TcpHeader tcpH;
  pktCopy->RemoveHeader (pppH);
  pktCopy->RemoveHeader (ipH);
  pktCopy->PeekHeader (tcpH);
  stringstream ss;
  tcpH.Print (ss);
  return ss.str ();
}

string
logPktIpv4Address (Ptr<const Packet> p)
{
  // debug 
  if(is_debug) cout << " Begin log header. " << endl;
  Ptr<Packet> pktCopy = p->Copy ();
  PppHeader pppH;
  Ipv4Header ipH;
  pktCopy->RemoveHeader (pppH);
  pktCopy->PeekHeader (ipH);
  stringstream ss;
  ipH.GetSource ().Print (ss);
  ss << " > ";
  ipH.GetDestination ().Print (ss);
  return ss.str ();
}

# endif