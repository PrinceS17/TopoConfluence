'''
This script is used to parse ITZ topology given the graphml file path
and output path. The generated xml file will be under the output folder.
Note that it basically works under python2.7 due to fnss's requirement. 

(If you want it to work under python3 as I do, you need to patch the file
parsers.py and topology.py in .local/lib/python3.5/site-packeges/fnss/
topologies/, where you replace all ".node[" with ".nodes[". The reason 
is networkx.classes.multidigraph.MultiDiGraph uses .nodes in python3
instead of .node/.nodes. which it did in python2.)

Last updated:       12.31.2019      Initial tests passed.

Current problem: 
    1) delay cannot be related to position currently.
    2) buffer size needs more careful setting.
'''
import os, sys
import fnss
import networkx as nx
from math import log


def parse(topo_path, xml_path, delay, buffer_type):
    topology = fnss.parse_topology_zoo(topo_path)
    topology = topology.to_undirected()
    fnss.set_capacities_edge_betweenness(topology, [200, 500, 1000], 'Mbps')        # TODO: hardcode now
    fnss.set_weights_constant(topology, 1)
    fnss.set_delays_constant(topology, delay, 'ms')
    if buffer_type == 'bdp':
        fnss.set_buffer_sizes_bw_delay_prod(topology, 'packet', 1500)
    elif buffer_type == 'bw':
        fnss.set_buffer_sizes_link_bandwidth(topology, buffer_unit='packet')
    else:
        fnss.set_buffer_sizes_constant(topology, 1500, 'packet')
    fnss.write_topology(topology, xml_path)


def main():
    topo_path = sys.argv[1]
    rev = topo_path[::-1]
    pos = rev.find('/')
    name = topo_path[len(topo_path) - pos:len(topo_path) - 8]
    delay = 2
    buffer_type = 'bdp'

    read_delay, read_buffer = False, False
    for arg in sys.argv[3:]:
        if arg == '-d':
            read_delay = True
        elif arg == '-b':
            read_buffer = True
        elif read_delay:
            delay = float(arg)
            read_delay = False
        elif read_buffer:
            buffer_type = arg
            read_buffer = False

    filename = '%s-%sms-%s.xml' % (name, delay, buffer_type)
    xml_path = os.path.join(sys.argv[2], filename)
    parse(topo_path, xml_path, delay, buffer_type)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print('Usage: python %s TOPOLOGY_PATH XML_FOLDER_PATH [-d DELAY] [-b BUFFER_TYPE]' % sys.argv[0])
        exit(1)
    main()
