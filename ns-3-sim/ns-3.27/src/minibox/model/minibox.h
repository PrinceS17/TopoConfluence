/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MINIBOX_H
#define MINIBOX_H

#include <string>
#include <iostream>
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/tap-bridge-module.h"
#include "ns3/applications-module.h"
#include "ns3/packet-sink.h"
#include "ns3/ppbp-application-module.h"

using namespace std;

namespace ns3 {

enum HeaderType {PPP, ETHERNET};

class AckAnalysis
{
public:
    AckAnalysis () {};
    AckAnalysis (uint32_t n);                           //!< use nSender to initialize all paramters
    bool insert(Ptr<Packet> p, uint32_t index);         //!< obtain IP destination and insert to the addr2index map, return false if failed
    int extract_index(Ptr<Packet> p);                   //!< extract index from addr2index, return -1 if failed
    void insert_pkt(uint32_t i, uint32_t No);        //!< insert sent pkt seq No. into seqNo table
    void push_back(uint32_t i, uint32_t No);            //!< push back mDrop seq No to mDrop table
    bool update(uint32_t i, uint32_t No);               //!< update last ack No and times, return false if failed
    bool update_udp(uint32_t i, uint32_t No);           //!< update the ack No for UDP flows, return true if ack is among rwnd
    uint32_t update_udp_drop(uint32_t i, uint32_t No);  //!< update the last ack No and return drop number
    uint32_t count_mdrop(uint32_t i);                   //!< count the mdrop for flow i
    bool clear(uint32_t i, uint32_t No);                //!< clear mDrop No table in case there are too many entries < No given
    void clear_seq();                                   //!< clear seqNo table at the end of each interval

    uint32_t get_lastNo(uint32_t i);
    uint32_t get_times(uint32_t i);
    vector<uint32_t> get_mDropNo(uint32_t i);
    map<uint32_t, uint32_t> get_map();


private:
    uint32_t nSender;
    vector< vector<uint32_t> > mDropNo;         //!< mDrop No. table to cancel later record
    vector< vector<uint32_t> > seqNo;           //!< seq No.. table for acks
    vector<uint32_t> startNo;                   //1< start Ack No. for current lDrop session
    vector<uint32_t> lastNo;                    //!< last Ack No.
    vector<uint32_t> times;                     //!< appearing times of ack
    map<uint32_t, uint32_t> addr2index;         //!< map from address to index

};


class Flow : public Object
{
public:
    Flow (Ptr<Node> txLeaf, Ptr<Node> rxLeaf, Ipv4AddressHelper leftAdd, Ipv4AddressHelper rightAdd, uint32_t rate, vector<double> times, \
        uint32_t maxBytes = 10000000, uint16_t port = 9, string typeId = "ns3::TcpSocketFactory");
    NetDeviceContainer build (string rate = "10Gbps", string dsRate = "");
    void setOnoff ();
    void setBulk ();
    void setPpbp (uint32_t nArrival = 40, double duration = 0.1);
    void setTapBridge (string tap_left, string tap_right);

    Ipv4AddressHelper getLeftAddr () { return leftAddr; }
    Ipv4AddressHelper getRightAddr () { return rightAddr; }
    Ptr<Node> getHost (uint32_t i);
    Ptr<NetDevice> getEndDevice (uint32_t i);

protected:
    void setSink ();

private:
    NodeContainer leafNode;           // [tx leaf, rx leaf]
    NodeContainer endNode;            // [tx end host, rx end host]
    NetDeviceContainer device;        // [tx-end tx, tx-leaf rx, rx-leaf tx, rx-end rx]
    Ipv4InterfaceContainer interface; // same order as above
    Ipv4AddressHelper leftAddr, rightAddr;

    uint16_t port;
    string typeId;            // Tcp/Udp Socket Factory
    uint32_t rate;              // traffic sending rate
    uint32_t maxBytes;        // max bytes for bulk application
    vector<double> times;     // [tStart, tStop]

};

class MiniBox : public Object
{
public:
    MiniBox () {};
    MiniBox (vector<uint32_t> id, double period = 0.1, double a = 0.2, double b = 0.02);             // set up output stream
    virtual ~MiniBox ();        // copy & move ctor might be needed...
    void install (Ptr<Node> node, Ptr<NetDevice> device, HeaderType htype = ETHERNET);          // connect to node, device & socket (trace)
    void start (Time t);                                            // bug: cannot start at 0.0s, must be positive time
    void stop (Time t);
    void connect ();
    void disconnect ();

    void onMacTx (Ptr<const Packet> p);                             // update # sent packets
    void onMacRx (Ptr<const Packet> p);                             // update ACK, use AckAnalysis for loss update
    void onRttChange (string context, Time oldRtt, Time newRtt);                    // update RTT
    void onRxAck (string context, SequenceNumber32 vOld, SequenceNumber32 vNew);    // update current ACK No. at a certain time
    void onLatency (string context, Time oldLat, Time newLat);                      // update latency
    void update ();                                                 // periodically update LLR and wnd
    // void setSocket (Ptr<Socket> socket);                         // in case GetObject doesn't work

    // for debug only
    void onCwnd (string context, uint32_t oldCwnd, uint32_t newCwnd);

private:
    vector<uint32_t> m_id;           // [run id, flow/socket id]
    string m_sktPath;           // socket path
    string m_devPath;           // device path
    HeaderType m_hType;           // Layer 2 header type
    Ptr<Node> m_node;
    Ptr<NetDevice> m_device;
    Ptr<Socket> m_socket;
    SequenceNumber32 m_ackNo;   // current ACK, sync latency with packet

    double m_period;            // time for LLR update
    double m_a;                 // EMA factor for RTT (new sample weight)
    double m_b;                 // EMA factor for LLR
    uint32_t m_rwnd = 0;
    uint32_t m_drop = 0;
    double m_rtt = 0;
    double m_llr = 0;

    vector<ofstream> m_fout;
    EventId m_startEvent;
    EventId m_stopEvent;
    bool m_isRunning = false;
    AckAnalysis m_acka;

};


}

#endif /* MINIBOX_H */

