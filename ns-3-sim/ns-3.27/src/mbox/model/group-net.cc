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
 * Author: Jinhui Song<jinhuis2@illinois.edu>
 */

#include <cmath>
#include <iostream>
#include <sstream>

// ns3 includes
#include "ns3/log.h"
#include "ns3/point-to-point-dumbbell.h"
#include "ns3/constant-position-mobility-model.h"

#include "ns3/node-list.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/vector.h"
#include "ns3/ipv6-address-generator.h"

// include
#include "group-net.h"

using namespace std;

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("GroupNetHelper");

GroupNetHelper::GroupNetHelper(vector<Group> grps, vector<uint32_t> cobnNo, vector<uint32_t> rxIds,
                               PointToPointHelper normalLink, vector<PointToPointHelper> bnLinks)
{
    nTx = 0;
    nRx = 0;
    nMid = 0;
    nRouter = 0;
    this->grps = grps;

    // build the id-index and id-node count map from grps
    for(uint32_t i = 0; i < grps.size(); i ++)
    {
        Group g = grps.at(i);
        for(uint32_t j = 0; j < g.txId.size(); j ++)
        {
            id2index[g.txId.at(j)] = {i, 0, j};         // 0: tx node
            id2cnt[g.txId.at(j)] = {0, nTx ++};
        }
        for(uint32_t h = 0; h < g.routerId.size(); h ++)
        {
            id2index[g.routerId.at(h)] = {i, 1, h};     // 1: main router
            id2cnt[g.routerId.at(h)] = {1, nRouter ++}; // main router (given by group) counts first
        }
        for(uint32_t k = 0; k < g.rxId.size(); k ++)
        {
            id2index[g.rxId.at(k)] = {i, 2, k};         // 2: mid
            id2cnt[g.rxId.at(k)] = {2, nMid};
            midCnt2id[nMid ++] = g.rxId.at(k);
        }
    }
    for(uint32_t i = 0; i < rxIds.size(); i ++)
    {
        id2cnt[rxIds.at(i)] = {3, nRx ++};
    }

    

    // update numbers of nodes
    nMainRouter = nRouter;
    nRouter += 2;           // 2 for co-bottleneck link
    // nRx = nMid;             // equal here for convenience

    this->cobnNo = cobnNo;

    // create the nodes
    mainRouter.Create(nMainRouter);
    cobnRouter.Create(2);
    txNode.Create(nTx);
    midNode.Create(nMid);
    rxNode.Create(nRx);

    // add bottleneck links
    for(uint32_t i = 0; i < nMainRouter / 2; i ++)
    {
        NetDeviceContainer temp = bnLinks.at(i).Install(mainRouter.Get(2*i), mainRouter.Get(2*i + 1));
        mainRouterDev.Add(temp);
    }
    cobnRouterDev = bnLinks.at(nMainRouter / 2).Install(cobnRouter);    // the last bnLink
    routerDev.Add(mainRouterDev);
    routerDev.Add(cobnRouterDev);

    // add common links at main part
    for(uint32_t i = 0; i < nMainRouter / 2; i ++)
    {
        Group g = grps.at(i);
        for(uint32_t j = 0; j < g.txId.size(); j ++)
        {
            uint32_t idx = id2cnt[g.txId.at(j)].at(1);                  // careful here!
            NetDeviceContainer temp = normalLink.Install(txNode.Get(idx), mainRouter.Get(2*i));
            txDev.Add(temp.Get(0));
            rxMainRouterDev.Add(temp.Get(1));
        }
        for(uint32_t k = 0; k < g.rxId.size(); k ++)
        {
            uint32_t idx = id2cnt[g.rxId.at(k)].at(1);
            NetDeviceContainer temp = normalLink.Install(mainRouter.Get(2*i + 1), midNode.Get(idx));
            txMainRouterDev.Add(temp.Get(0));
            rxMidDev.Add(temp.Get(1));
        }
    }

    // add common links at co-bottleneck tx and rx side
    uint32_t t = 0;
    for(auto id:cobnNo)
    {
        uint32_t idx = id2cnt[id].at(1);
        NetDeviceContainer temp = normalLink.Install(midNode.Get(idx), cobnRouter.Get(0));
        txMidDev.Add(temp.Get(0));
        rxCobnRouterDev.Add(temp.Get(1));
        
        NetDeviceContainer temp1 = normalLink.Install(cobnRouter.Get(1), rxNode.Get(idx));
        txCobnRouterDev.Add(temp1.Get(0));
        rxDev.Add(temp.Get(1));

        id2devCnt[id] = t;
        t ++;
    }

    // add common links at cobn part except the co-bottleneck link
    for(int i = 0; i < nMid; i ++)
    {   
        // we need find here since we need find the index of node to install p2p link
        if(find(cobnNo.begin(), cobnNo.end(), midCnt2id[i]) == cobnNo.end())
        {
            NetDeviceContainer temp = normalLink.Install(midNode.Get(i), rxNode.Get(i));
            txMidDev.Add(temp.Get(0));
            rxDev.Add(temp.Get(1));

            id2devCnt[midCnt2id.at(i)] = t;
            t ++;
        }
    }
}

GroupNetHelper::~GroupNetHelper() {}

Ptr<Node> GroupNetHelper::GetSender(uint32_t id) const 
{
    vector<uint32_t> idxs = id2cnt.at(id);
    NS_ASSERT_MSG(idxs.at(0) == 0, "This is not a sender ID!");
    return txNode.Get(idxs.at(1));
}

Ptr<Node> GroupNetHelper::GetSenderByIndex(uint32_t i) const
{
    return txNode.Get(i);
}

Ptr<Node> GroupNetHelper::GetReceiver(uint32_t id) const 
{
    vector<uint32_t> idxs = id2cnt.at(id);
    NS_ASSERT_MSG(idxs.at(0) == 3, "This is not a receiver ID!");
    return rxNode.Get(idxs.at(1));
}

Ptr<Node> GroupNetHelper::GetReceiverByIndex(uint32_t i) const
{
    return rxNode.Get(i);
}

Ptr<Node> GroupNetHelper::GetTxRouter(uint32_t id) const
{
    vector<uint32_t> idxs = id2cnt.at(id);
    NS_ASSERT_MSG(idxs.at(0) == 1, "This is not a main router ID!" );
    NS_ASSERT_MSG(idxs.at(1) % 2 == 0, "This is not a tx router!");
    return mainRouter.Get(idxs.at(1));
}

Ptr<Node> GroupNetHelper::GetTxRouterByIndex(uint32_t i) const
{
    NS_ASSERT_MSG(i % 2 == 0, "This is not a tx router!");
    return mainRouter.Get(i);
}

Ipv4Address GroupNetHelper::GetTxIpv4Address(uint32_t id) const
{
    vector<uint32_t> idxs = id2cnt.at(id);
    NS_ASSERT_MSG(idxs.at(0) == 0, "This is not a sender ID!");
    return txInterfaces.Getaddress(idxs.at(1));
}

Ipv4Address GroupNetHelper::GetRxIpv4Address(uint32_t id) const
{
    uint32_t idx = id2devCnt.at(id);
    return rxInterfaces.Getaddress(idx);
}

Ipv4Address GroupNetHelper::GetTxIpv6Address(uint32_t id) const
{
    vector<uint32_t> idxs = id2cnt.at(id);
    NS_ASSERT_MSG(idxs.at(0) == 0, "This is not a sender ID!");
    return txInterfaces6.Getaddress(idxs.at(1));
}

Ipv4Address GroupNetHelper::GetRxIpv6Address(uint32_t id) const
{
    uint32_t idx = id2devCnt.at(id);
    return rxInterfaces6.Getaddress(idx);
}


uint32_t GroupNetHelper::GetNSender() const
{
    return nTx;
}

uint32_t GroupNetHelper::GetNReceiver() const
{
    return nRx;
}

void GroupNetHelper::InstallStack(InternetStackHelper stack)
{
    stack.Install(txNode);
    stack.Install(midNode);
    stack.Install(rxNode);
    stack.Install(mainRouter);
    stack.Install(cobnRouter);
}

void GroupNetHelper::AssignIpv4Address(Ipv4AddressHelper txIp,
                                       Ipv4AddressHelper midIp,
                                       Ipv4AddressHelper cobnIp,
                                       Ipv4AddressHelper normalIp,
                                       Ipv4AddressHelper routerIp)
{
    // routerIp: assign all the routers (side between routers), order: main part, cobn part
    routerInterfaces = routerIp.Assign(routerDev);

    for(uint32_t i = 0; i < nTx; i ++)
    {
        // txIp: tx to rxMainRouter
        NetDeviceContainer ndc;
        ndc.Add(txDev.Get(i));
        ndc.Add(rxMainRouterDev.Get(i));      // should equal to txDev since they are installed together
        Ipv4InterfaceContainer ifc = txIp.Assign(ndc);
        txInterfaces.Add(ifc.Get(0));
        rxMainRouterInterfaces.Add(ifc.Get(1));
        txIp.NewNetwork();                      // interesting
    }
    for(uint32_t i = 0; i < nMid; i ++)
    {
        // midIp: txMainRouter to rxMid
        NetDeviceContainer ndc;
        ndc.Add(txMainRouterDev.Get(i));
        ndc.Add(rxMidDev.Get(i));
        Ipv4InterfaceContainer ifc = midIp.Assign(ndc);
        txMainRouterInterfaces.Add(ifc.Get(0));
        rxMidInterfaces.Add(ifc.Get(1));
        midIp.NewNetwork();
    }
    
    // cobn links pushed first, so first cobnNo.size() Get()
    for(unit32_t i = 0; i < cobnNo.size(); i ++)    
    {
        // cobnIp: cobn's mid node to cobn router
        NetDeviceContainer ndc;
        
        ndc.Add(txMidDev.Get(i));
        ndc.Add(rxCobnRouterDev.Get(i));
        Ipv4InterfaceContainer ifc = cobnIp.Assign(ndc);
        txMidInterfaces.Add(ifc.Get(0));
        rxCobnRouterInterfaces.Add(ifc.Get(1));
        cobnIp.NewNetwork();

        // cobnIp: cobn router to rx node 
        NetDeviceContainer ndc1;
        ndc1.Add(txCobnRouterDev.Get(i));
        ndc1.Add(rxDev.Get(i));
        Ipv4InterfaceContainer ifc = cobnIp.Assign(ndc1);
        txCobnRouterInterfaces.Add(ifc.Get(0));
        rxInterfaces.Add(ifc.Get(1));
        cobnIp.NewNetwork();
    }

    // we don't need find mid id, because the devices are reordered and first n is cobn dev!!
    for(uint32_t i = cobnNo.size(); i < nMid; i ++)
    {
        // normalIp: normal links
        NetDeviceContainer ndc;
        ndc.Add(txMidDev.Get(i));
        ndc.Add(rxDev.Get(i));
        Ipv4InterfaceContainer ifc = normalIp.Assign(ndc);
        txMidInterfaces.Add(ifc.Get(0));
        rxInterfaces.Add(ifc.Get(1));
        normalIp.NewNetwork();
    }
}

void GroupNetHelper::AssignIpv6Address (Ipv6Address addrBase, Ipv6Prefix prefix)
{
    // assign the router network
    Ipv6AddressGenerator::Init (addrBase, prefix);
    Ipv6Address v6network = Ipv6AddressGenerator::GetNetwork(prefix);
    Ipv6AddressHelper ih;
    ih.SetBase(v6network, prefix);
    routerInterfaces6 = ih.Assign(routerDev);
    Ipv6AddressGenerator::NextNetwork(prefix);

    // tx to rxMain
    for(uint32_t i = 0; i < nTx; i ++)
    {
        v6network = Ipv6AddressGenerator::GetNetwork(prefix);
        ih.SetBase(v6network, prefix);

        NetDeviceContainer ndc;
        ndc.Add(txDev.Get(i));
        ndc.Add(rxMainRouterDev.Get(i));
        Ipv6InterfaceContainer ifc = ih.Assign(ndc);
        Ipv6InterfaceContainer::Iterator it = ifc.Begin();
        txInterfaces6.Add((*it).first, (*it).second);
        it ++;
        rxMainRouterInterfaces6.Add((*it).first, (*it).second);
        Ipv6AddressGenerator::NextNetwork(prefix);
    }

    // txMain to rxMid
    for(uint32_t i = 0; i < nMid; i ++)
    {
        v6network = Ipv6AddressGenerator::GetNetwork(prefix);
        ih.SetBase(v6network, prefix);

        NetDeviceContainer ndc;
        ndc.Add(txMainRouterDev.Get(i));
        ndc.Add(rxMidDev.Get(i));
        Ipv6InterfaceContainer ifc = ih.Assign(ndc);
        Ipv6InterfaceContainer::Iterator it = ifc.Begin();
        txMainRouterInterfaces6.Add((*it).first, (*it).second);
        it ++;
        rxMidInterfaces6.Add((*it).first, (*it).second);
        Ipv6AddressGenerator::NextNetwork(prefix);
    }    

    // cobn link node: mid to cobn router
    for(uint32_t i = 0; i < cobnNo.size(); i ++)
    {
        v6network = Ipv6AddressGenerator::GetNetwork(prefix);
        ih.SetBase(v6network, prefix);

        NetDeviceContainer ndc;
        ndc.Add(txMidDev.Get(i));
        ndc.Add(rxCobnRouterDev.Get(i));
        Ipv6InterfaceContainer ifc = ih.Assign(ndc);
        Ipv6InterfaceContainer::Iterator it = ifc.Begin();
        txMidInterfaces6.Add((*it).first, (*it).second);
        it ++;
        rxCobnRouterInterfaces6.Add((*it).first, (*it).second);
        Ipv6AddressGenerator::NextNetwork(prefix);

        NetDeviceContainer ndc1;
        ndc1.Add(txCobnRouterDev.Get(i));
        ndc1.Add(rxDev.Get(i));
        Ipv6InterfaceContainer ifc = ih.Assign(ndc1);
        Ipv6InterfaceContainer::Iterator it = ifc.Begin();
        txCobnRouterInterfaces6.Add((*it).first, (*it).second);
        it ++;
        rxInterfaces6.Add((*it).first, (*it).second);
        Ipv6AddressGenerator::NextNetwork(prefix);        
    }

    // normal link node: txMid to rx
    for(uint32_t i = cobnNo.size(); i < nMid; i ++)
    {
        v6network = Ipv6AddressGenerator::GetNetwork(prefix);
        ih.SetBase(v6network, prefix);

        NetDeviceContainer ndc;
        ndc.Add(txMidDev.Get(i));
        ndc.Add(rxDev.Get(i));
        Ipv6InterfaceContainer ifc = ih.Assign(ndc);
        Ipv6InterfaceContainer::Iterator it = ifc.Begin();
        txMidInterfaces6.Add((*it).first, (*it).second);
        it ++;
        rxInterfaces6.Add((*it).first, (*it).second);
        Ipv6AddressGenerator::NextNetwork(prefix);
    }

}


}   // namespace ns3