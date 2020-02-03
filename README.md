# TopoConfluence: Synthetic Network Simulation Platform for Co-bottleneck Analysis

TopoConfluence is a synthetic simulation platform with various topologies and configurations to verify our fCoder design.
It uses the topologies from Internet Topology Zoo (ITZ) \[1\] and Brite Topology Generator \[2\], configures the network (FNSS \[3\] for ITZ) and then use ns-3 \[4\] to simulate.
After simulation, statistics like RTT are collected and bottleneck link of each flow is recorded for later usage (in our case, train a co-bottleneck detector).


## Installation
Install python3 and pip3 by (Ubuntu assumed)

    sudo apt-get install python3 python3-pip

Required python3 packeges include FNSS, networkx, numpy, xmltodict, etc. Install them by

    pip3 install -r requirements.txt

No explicit installation of Brite or ns3 is required if you run simTop.py first.
Specifically, it will help you download ITZ topology(if there isn't any), make Brite(without `--run-only`), and configure ns-3(if not before and without `--dry-run`).

You can run `python3 simTop.py` at the beginning to get these all done. 


## Simulation with Internet Topology Zoo

The main procedures:

* Download the ITZ dataset and randomly sample some topologies (in `simTop.py`);
* Parse the topologies and configure the networks (delay, backbone bandwidth) by FNSS parser (in `src/topoSurfer.py`);
* Configure ns3 (in `simTop.py`);
* Run simulations with choice of flow settings and leaf bandwidth (in `src/confluentSim.py`).

As a user, the only thing we need to do is to use `simTop.py` with right arguments. The following way is tested.

**First**, Generate 64 topologies randomly without running simulation over them:

    python3 simTop.py -s 64 --dry-run

After this step, 64 topologies are collected and parsed to XML files stored in `TopoSurfer/xml` for later use.

**Second**, Run over a specific topology (i.e. 27th XML file) with flow number ranging from 10 to 16 using 64 processes

    python3 simTop.py -c 64 -r 10:16 --run-only -x 27

It will use choose 1 from existing XML files and run 7 * 9 = 63 runs on 64 cores 
(default 9 different settings for each flow number, see `src/confluentSim.py` for more detail). 
**Note** that if you don't specify `--run-only` when there are existing topologies in `TopoSurfer/xml`, the script will again add some random topologies, **increasing the total number of topologies**.

**At last**, data will be collected in `Flows_MIN:MAX_XML-NAME/`. 

`dat/` stores statistics per flow in files like `DATA_MID_FLOW-NUM.dat`.
For instance, RttLlr_5847_0.dat means Rtt and Llr data of flow 0 in run 5847. The internal row format of files **except bottleneck** is 

    Time Data [Data2 if exists]

Row format of `bottleneck_MID.txt` is as follows:

    Flow Bottleneck_link               # 4294967295 or -1 for no bottleneck

`info/` stores flow settings for each run in `flow_info_MID.txt`. The internal row format is

    Source Destination Leaf_bandwidth  # target flows
    Source Destination                 # cross traffic

`log/` stores the ns3 logs, which are useful for debugging.


## Simulation with Brite Topology

Our simulation with Brite topology mainly focuses on the dumbbell structure between 2 ASes, in which case we can control
the co-bottleneck we want better.
Since ns3 integrated Brite module, we don't need a parser like FNSS to help us parse the topology. So the main procedures:

* Configure the network (inter-AS bandwidth) by generating the Brite configure file;
* Scan parameters (edge bandwidth, cross traffic) and run ns3 simulations.

See our paper for more detail about the network settings.

As a user, to simulate a network with 7 target flows with random seed 20 using 24 processes:

    python3 simBrite.py -c 24 -r 7:7 -b 20


## Reference
\[1\] Knight, Simon, et al. "The internet topology zoo." IEEE Journal on Selected Areas in Communications 29.9 (2011): 1765-1775.

\[2\] Medina, Alberto, Ibrahim Matta, and John Byers. "BRITE: a flexible generator of Internet topologies." (2000).

\[3\] L. Saino, C. Cocora, G. Pavlou, A Toolchain for Simplifying Network Simulation Setup, in Proceedings of the 6th International ICST Conference on Simulation Tools and Techniques (SIMUTOOLS '13), Cannes, France, March 2013

\[4\] Riley, George F., and Thomas R. Henderson. "The ns-3 network simulator." Modeling and tools for network simulation. Springer, Berlin, Heidelberg, 2010. 15-34.
