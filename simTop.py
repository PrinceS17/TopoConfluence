'''
This script is the top script of simulation experiments, which will help
you import topologies from Internet Topology Zoo or generate them by 
Brite topology generator, parse the topology, generate random experiment
settings and finally do the ns-3 simulation to collect data (mainly RTT).

In detail, it will achieve the following

0. Configure the environments
    TODO: current requirements have some redundancy.

1. For ITZ simulations:
    1) Use topoSurfer.py to import topologies and parse them to XML files;
    2) Use confluentSim.py to generate settings and parallelize ns-3 
        simulations.
    TODO: modify the default path value inside each script

2. TODO For Brite simulations:
    1) TODO generate Brite configurations;
    2) Do the ns-3 simulations.


'''

import os, sys
import subprocess as sp


def test_ls(path=None):
    path = path if path else ''
    tmp = os.popen('ls %s' % path).read()
    tlist = tmp.strip().split('\n')
    return tlist


def printHelp():
    # print help message & exit
    print('Usage: python %s [-s SAMPLE_NUM] [-c PROCESS_NUM] [-r MIN:MAX] [-h]' % sys.argv[0])
    print('       SAMPLE_NUM:   Number of topologies samples (default is 30);')
    print('       PROCESS_NUM:  Number of processes for multiprocessing (default is 8);')
    print('       MIN:MAX:      Range of number of target flows (default is 7:8).')
    exit(1)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        printHelp()

    opt_map = {'-s':30, '-c':8, '-r':'7:8'}     # TODO: use option to run Brite or configure
    cur_option = None
    dry_run = False
    for arg in sys.argv[1:]:
        if arg == '--dry-run':
            dry_run = True
        elif arg == '-h':
            printHelp()
        elif arg in opt_map:
            cur_option = arg
        elif cur_option in opt_map:
            opt_map[cur_option] = arg
            cur_option = None
        else:
            print('Error: no such argument!')
            exit(1)

    # set path
    pos = os.getcwd().find('Confluence')
    pos += len('Confluence')
    root_folder = os.getcwd()[:pos]
    print('-> Root folder: %s' % root_folder)

    os.chdir(root_folder)
    assert 'ns-3-sim' in test_ls()
    ns3_path = os.path.join(root_folder, 'ns-3-sim', 'ns-3.27')
    assert 'topo-parse.py' in test_ls('src')
    fnss_path = os.path.join(root_folder, 'src', 'topo-parse.py')
    print(os.getcwd())
    topo_path = 'download'      # enable the download of ITZ in topoSurfer.py
    if 'TopoSurfer' in test_ls():
        os.chdir('TopoSurfer')
        if 'topologyzoo' in test_ls():
            topo_path = os.path.join(root_folder, 'TopoSurfer', 'topologyzoo')     

    # topoSurfer
    os.chdir(root_folder)
    n_sample = opt_map['-s']
    surf_path = os.path.join(root_folder, 'src', 'topoSurfer.py')
    surf_cmd = 'python3 %s -r %s -f %s -t %s -s %s' \
        % (surf_path, root_folder, fnss_path, topo_path, n_sample)
    try:
        os.system(surf_cmd)
        print('-> TopoSurfer complete: topologies are collected and parsed.')
    except:
        print('-> TopoSurfer not complete! Exit.')
        exit(1)

    # configure BRITE for ns-3
    os.chdir(os.path.join(root_folder, 'BRITE'))
    os.system('make clean')
    os.system('make')

    # configure ns-3
    os.chdir(ns3_path)
    if 'MboxStatistics' not in test_ls():
        os.mkdir('MboxStatistics')
        os.mkdir('MboxFig')
    conf_cmd = 'CXXFLAGS="-Wall" ./waf configure --with-brite=../../BRITE --enable-examples'
    tmp = sp.getoutput('./waf --check-config | grep Examples')
    if not dry_run and 'enabled' not in tmp:
        try:
            os.system(conf_cmd)
        except:
            print('-> ns-3 configure error! Exit.')
            exit(1)

    # confluentSim
    os.chdir(root_folder)
    n_core = opt_map['-c']
    # n_min, n_max = [int(r) for r in opt_map['-r'].split(':')]
    assert 'xml' in test_ls('TopoSurfer')
    xml_folder = os.path.join(root_folder, 'TopoSurfer', 'xml')
    xmls = test_ls(xml_folder)
    for xml in xmls:
        path = os.path.join(xml_folder, xml)
        confl_cmd = 'python3 src/confluentSim.py -r %s -x %s -c %s' \
            % (opt_map['-r'], path, n_core)
        print('-> ConfluentSim begin running for %s ...\n' % xml)
        if not dry_run:
            os.system(confl_cmd)
    




