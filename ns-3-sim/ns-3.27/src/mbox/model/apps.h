#ifndef APPS_H
#define APPS_H

#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

# include <vector>
#include "ns3/tag.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"

using namespace std;

namespace ns3{


//=========================================================================//
//=========================Begin of TAG definition=========================//
//=========================================================================//
class MyTag : public Tag
{
public:
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (TagBuffer i) const;
  virtual void Deserialize (TagBuffer i);
  virtual void Print (std::ostream &os) const;

  // these are our accessors to our tag structure
  void SetSimpleValue (uint32_t value);
  uint32_t GetSimpleValue (void) const;
private:
  uint32_t m_simpleValue;
};

class MyApp : public Application 
{
public:

  MyApp ();
  virtual ~MyApp();

  //void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate);
  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, DataRate dataRate);
  void StartAck(void);
  void SetTagValue(uint32_t value);
  void SetDataRate(DataRate rate);
  Ptr<Socket> GetSocket();

  void SendAck (uint32_t ackNo);

  uint32_t tagScale = 1000000;
  bool isTrackPkt;          // configure if tract the pkt

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);
  
  

  void ScheduleTx (void);
  void SendPacket (void);

  Ptr<Socket>     m_socket;
  Address         m_peer;
  uint32_t        m_packetSize;
  //uint32_t        m_nPackets;
  DataRate        m_dataRate;
  EventId         m_sendEvent;
  bool            m_running;
  //uint32_t        m_packetsSent;
  uint32_t        m_tagValue;
  uint32_t m_cnt;        // count of packet sent by this app
  uint32_t m_rid;         // random ID

};


}

#endif
