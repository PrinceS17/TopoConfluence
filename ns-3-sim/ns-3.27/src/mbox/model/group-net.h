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

#ifndef GROUP_NET_H
#define GROUP_NET_H

#include <cstring>
#include <cmath>
#include <iostream>
#include <sstream>
#include <map>
#include <cstdio>
#include <vector>
#include "ns3/mrun.h"


#include "ns3/log.h"
#include "ns3/point-to-point-dumbbell.h"
#include "ns3/constant-position-mobility-model.h"

#include "ns3/node-list.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/vector.h"
#include "ns3/ipv6-address-generator.h"

#include "point-to-point-helper.h"
#include "ipv4-address-helper.h"
#include "ipv6-address-helper.h"
#include "internet-stack-helper.h"
#include "ipv4-interface-container.h"
#include "ipv6-interface-container.h"

namespace ns3 {

/**
 * \ingroup mbox
 * 
 *  Tx0 --\                                    /-- Mid0 --       [normal p2p]                      -- Rx0
 *         - router0 --[bottleneck]-- router1 -
 *  Tx1 __/                                    \__ Mid1 --\                                       /-- Rx1
 *                                                         - router5 --[co-bottleneck]-- router6 -
 *  Tx2 --\                                    /-- Mid2 __/                                       \__ Rx2   
 *         - router2 --[bottleneck]-- router3 -
 *  Tx3 __/                                    \__ Mid3 __       [normal p2p]                      __ Rx3
 * 
 * Name of container:
 *  txNode | mainRouter[even]  | mainRouter[odd] | midNode | cobnRouter[even]   |   cobnRouter[odd] | rxNode
 * 
 * \brief A helper to generate a group network for MiddlePolice simulation. 
 * A group network is a network that has N sources and N destinations and 
 * there're links between them. The exact topology inside is subject to change
 * of simulation. Note it's a network of not only one group but groups.
 */

class GroupNetHelper
{
public:
    /**
     * Create a group network given the groups' specification, including # sender,
     * # receiver, connections and the parameter information like port and rate.
     * 
     * \param grps Group vector that include the main/independent part of the network.
     * \param cobnNo Vector of the mid node Id. at the left of cobottleneck link. (Build only 1 cobottleneck now) 
     * \param rxIds Receiver Id of the whole network. 
     * \param normalLink Link for common p2p link.
     * \param bnLinks Bottleneck links (so we don't bother setting parameters here).
     */
    GroupNetHelper (vector<Group> grps, vector<uint32_t> cobnNo, vector<uint32_t> rxIds,
                    PointToPointHelper normalLink, vector<PointToPointHelper> bnLinks);
    ~GroupNetHelper ();

    Ptr<Node> GetSender(uint32_t id) const;
    Ptr<Node> GetSenderByIndex(uint32_t i) const;
    Ptr<Node> GetReceiver(uint32_t id) const;
    Ptr<Node> GetReceiverByIndex(uint32_t i) const; 
    Ptr<Node> GetTxRouter(uint32_t id) const;       //!< get the tx router to deploy mbox on it
    Ptr<Node> GetTxRouterByIndex(uint32_t i) const;

    uint32_t GetNSender() const;
    uint32_t GetNReceiver() const;

    Ipv4Address GetTxIpv4Address(uint32_t id) const;
    Ipv4Address GetRxIpv4Address(uint32_t id) const;
    Ipv4Address GetTxIpv6Address(uint32_t id) const;
    Ipv4Address GetRxIpv6Address(uint32_t id) const;
    
    void InstallStack (InternetStackHelper stack);
    /**
     * Assign Ipv4 addresses to all the interfaces.
     * 
     */
    void GroupNetHelper::AssignIpv4Address(Ipv4AddressHelper txIp,
                                           Ipv4AddressHelper midIp,
                                           Ipv4AddressHelper cobnIp,
                                           Ipv4AddressHelper normalIp,
                                           Ipv4AddressHelper routerIp); //!< router ips are ip of all internal routers
    
    void AssignIpv6Address (Ipv6Address network, Ipv6Prefix prefix);    //!< no need if too complicated


private:
    vector<Group> grps;
    map<uint32_t, vector<uint32_t>> id2index;       //!< store a map from node id to the pair <group i, type, node No.>, only work for main part!
    map<uint32_t, vector<uint32_t>> id2cnt;         //!< map from node id to {node type, count} 
    map<uint32_t, uint32_t> id2devCnt;              //!< map only for cobn part: id->dev/interfaces cnt
    map<uint32_t, uint32_t> midCnt2id;              //!< tool map: mid index -> mid node id
    uint32_t nTx;
    uint32_t nRx;
    uint32_t nMid;
    uint32_t nRouter;
    uint32_t nMainRouter;
    uint32_t nCobnRouter;

    //!< experimental No., denoted by RX indexes of the co-bottleneck link, only for 1 dumbbell (i.e. 1 co-bottleneck)
    vector<uint32_t> cobnNo;                

    NodeContainer txNode;
    NetDeviceContainer txDev;
    NodeContainer rxNode;
    NetDeviceContainer rxDev;
    NodeContainer midNode;                          //!< node in the middle column 
    NetDeviceContainer txMidDev;                    //!< tx mid means the right/sender side of mid node
    NetDeviceContainer rxMidDev;                    //!< rx mid means the left/receiver side of mid node
    NodeContainer router;
    NetDeviceContainer routerDev;                   //!< router dev, containing main router and cobn router's inside dev
    
    NodeContainer mainRouter;                       //!< router of independent groups on the left side
    NetDeviceContainer mainRouterDev;               //!< visit left and right by even and odd indexes
    NetDeviceContainer txMainRouterDev;             //!< connect with tx node
    NetDeviceContainer rxMainRouterDev;             //!< connect with mid node
    NodeContainer cobnRouter;                       //!< router of bottleneck on the right side
    NetDeviceContainer cobnRouterDev;
    NetDeviceContainer txCobnRouterDev;             //!< connect with mid node
    NetDeviceContainer rxCobnRouterDev;             //!< connect with rx node
    
    Ipv4InterfaceContainer txInterfaces;
    Ipv4InterfaceContainer rxInterfaces;
    Ipv4InterfaceContainer midInterfaces;
    Ipv4InterfaceContainer txMidInterfaces;
    Ipv4InterfaceContainer rxMidInterfaces;
    Ipv4InterfaceContainer txMainRouterInterfaces;
    Ipv4InterfaceContainer rxMainRouterInterfaces;
    Ipv4InterfaceContainer cobnRouterInterfaces;
    Ipv4InterfaceContainer txCobnRouterInterfaces;
    Ipv4InterfaceContainer rxCobnRouterInterfaces;
    Ipv4InterfaceContainer routerInterfaces;

    Ipv6InterfaceContainer txInterfaces6;
    Ipv6InterfaceContainer rxInterfaces6;
    Ipv6InterfaceContainer midInterface6s;
    Ipv6InterfaceContainer txMidInterfaces6;
    Ipv6InterfaceContainer rxMidInterfaces6;
    Ipv6InterfaceContainer txMainRouterInterfaces6;
    Ipv6InterfaceContainer rxMainRouterInterfaces6;
    Ipv6InterfaceContainer cobnRouterInterfaces6;
    Ipv6InterfaceContainer txCobnRouterInterfaces6;
    Ipv6InterfaceContainer rxCobnRouterInterfaces6;
    Ipv6InterfaceContainer routerInterfaces6;
};

} // namespace ns3

#endif /* POINT_TO_POINT_DUMBBELL_HELPER_H */
