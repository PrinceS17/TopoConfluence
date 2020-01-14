/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ratemonitor.h"
#include "ns3/tools.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RateMonitor");

RateMonitor::RateMonitor (vector<uint32_t> id, bool useRx, double period): m_id(id), m_period(period), \
    m_bytes (0), m_pktSize (1500), m_useRx (useRx), m_avgRate (0)
{
    NS_LOG_FUNCTION (id[0] << id[1] << period);
    string fname = "MboxStatistics/DataRate_" + to_string (id[0]) + "_" + to_string (id[1]) + ".dat";
    m_fout.open (fname, ios::out | ios::trunc);

} 

RateMonitor::~RateMonitor ()
{
    NS_LOG_FUNCTION (this);
    m_fout.close ();
}

void RateMonitor::install (Ptr<NetDevice> device)
{
    NS_LOG_FUNCTION (this);
    m_device = device;
}

void RateMonitor::start (Time t)
{
    NS_LOG_FUNCTION (t);
    NS_LOG_INFO ("- Start at: " << t.GetSeconds () << " s");
    
    m_isRunning = true;
    Simulator::Cancel (m_startEvent);
    m_startEvent = Simulator::Schedule (t, &RateMonitor::connect, this);
}

void RateMonitor::connect ()
{
    Simulator::Schedule (Seconds(0), &RateMonitor::monitor, this);
    if (m_useRx)
        m_device->TraceConnectWithoutContext ("MacRx", MakeCallback (&RateMonitor::onMacRx, this));
    else
        m_device->TraceConnectWithoutContext ("MacTx", MakeCallback (&RateMonitor::onMacRx, this));    
}

void RateMonitor::stop (Time t)
{
    NS_LOG_FUNCTION (t);
    Simulator::Schedule (t, &RateMonitor::disconnect, this);
}

void RateMonitor::disconnect ()
{
    NS_LOG_FUNCTION_NOARGS ();
    NS_LOG_INFO ("- Stop and disconnect. ");
    
    m_isRunning = false;
    if (m_useRx)
        m_device->TraceDisconnectWithoutContext ("MacRx", MakeCallback (&RateMonitor::onMacRx, this));
    else
        m_device->TraceDisconnectWithoutContext ("MacTx", MakeCallback (&RateMonitor::onMacRx, this));    
}

void RateMonitor::onMacRx (Ptr<const Packet> p)
{
    NS_LOG_FUNCTION (m_id[1] << m_bytes);
    uint32_t curSize = getTcpSizeEth (p);           // hardcode to be Ethernet
    if(m_pktSize != curSize)
    {
        m_pktSize = curSize;
        NS_LOG_FUNCTION("   - pkt size = " << m_pktSize);
    }
    m_bytes += (double) m_pktSize;
}

void RateMonitor::monitor ()
{
    NS_LOG_FUNCTION (m_id[1] << m_rate << m_bytes);
    if (m_isRunning)
        Simulator::Schedule (Seconds (m_period), &RateMonitor::monitor, this);
    
    m_rate = m_bytes * 8 / m_period / 1000;         // in kbps
    m_avgRate = !m_avgRate? m_rate : 0.8 * m_avgRate + 0.2 * m_rate;    // moving avg rate 
    m_bytes = 0;
    m_fout << Simulator::Now ().GetSeconds () << " " << m_rate << " kbps" << endl;
    NS_LOG_INFO (" - " << Simulator::Now ().GetSeconds () << "s flow " << m_id[1] \
        << ": " << m_rate << " kbps (run: " <<  m_id[0] << ")");
    
    double t = Simulator::Now().GetSeconds ();
    if (int(t * 10) % 2 == 0)
        NS_LOG_DEBUG (" - " << t << "s: run " << m_id[0] << ", flow " << m_id[1] \
            << ": " << m_rate << " kbps, MA: " << m_avgRate << " kbps");
}

double RateMonitor::getRate ()
{
    return m_rate;
}

void RateMonitor::getAvgRate (double& rate)
{
    NS_LOG_FUNCTION (m_avgRate);
    rate = m_avgRate;
}

}

