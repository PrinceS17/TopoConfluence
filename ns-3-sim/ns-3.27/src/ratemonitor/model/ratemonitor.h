/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef RATEMONITOR_H
#define RATEMONITOR_H

#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include "ns3/core-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
// #include "ns3/csma-module.h"
// #include "ns3/point-to-point-module.h"

using namespace std;

namespace ns3 {

class RateMonitor : public Object
{
public:
    RateMonitor () {};
    RateMonitor (vector<uint32_t> id, bool m_useRx = true, double period = 0.1);
    virtual ~RateMonitor ();
    void install (Ptr<NetDevice> device);
    void start (Time t);
    void stop (Time t);
    void connect ();
    void disconnect ();

    void onMacRx (Ptr<const Packet> p);
    void monitor ();
    double getRate ();
    void getAvgRate (double& rate);

private:
    vector<uint32_t> m_id;
    Ptr<NetDevice> m_device;
    bool m_useRx;
    double m_period;
    double m_rate;
    double m_avgRate;
    double m_bytes;
    uint32_t m_pktSize;
    
    bool m_isRunning;
    fstream m_fout;
    EventId m_startEvent;
    EventId m_stopEvent;

};


}

#endif /* RATEMONITOR_H */

