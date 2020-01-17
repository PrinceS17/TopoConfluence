'''
This script is used to simulate among different Brite topologies and scan 
parameters like the number target flows. Each run consists of these settings:

1. Specify the topology by determining Brite configure file (number of 
    leaf nodes, BW), and tid;
2. Specify the network parameters in ns-3, such as nNormal, nCross, etc;

TODO:
1. Integration test!
2. Run ns-3 to check!


TODO Things that need attention
a. solution of Brite conf: generate by modifying an example?
b. try the ns-3 in case of the error of insufficient leaf...
c. scan the AS number, and change AS[1] to AS[n-1] in ns-3.

'''

import os, sys, time, random
import subprocess as sp
from multiprocessing import Process, Lock

def test_ls(path=None):
    path = path if path else ''
    tmp = os.popen('ls %s' % path).read()
    tlist = tmp.strip().split('\n')
    return tlist


class Briter:
    # class for Brite configuration's API
    def __init__(self, folder, path=None):
        self.folder = folder
        self.path = path if path else os.path.join(folder, 'TD_DefaultWaxman.conf')
        with open(path, 'r') as f:
            self.conf = f.read()
        
    def setBwInter(self, bw):
        self.conf.replace('BWInterMin = 100.0', 'BWInterMin = %s' % bw)
    
    def setBwIntra(self, bw):
        self.conf.replace('BWIntraMin = 1000.0', 'BWIntraMin = %s' % bw)
    
    def setAsNum(self, n):
        self.conf.replace('N = 2', 'N = %s' % n)
    
    def setRouterNum(self, n):
        self.conf.replace('N = 32', 'N = %s' % n)
    
    def generate(self, name):
        out_path = os.path.join(self.folder, name)
        with open(out_path, 'w') as f:
            f.write(self.conf)
        return out_path


class simBrite:
    def __init__(self,
        N_range,
        sample='TD_DefaultWaxman.conf',
        root_folder=None,
        ns3_path=None
        ):
        self.N_min, self.N_max = N_range
        self.sample = sample
        if root_folder:
            os.chdir(root_folder)
        rname = 'Brite_%s:%s_%s' % (N_range[0], N_range[1], \
            time.strftime("%b-%d-%H:%M:%S"))
        os.mkdir(rname)
        self.root = os.path.join(os.getcwd(), rname)
        self.ns3_path = ns3_path if ns3_path else os.path.join(os.getcwd(), \
            'ns-3-sim', 'ns-3.27')
        os.chdir(rname)
        self.brt = Briter(os.path.join(self.ns3_path, 'brite_conf'))
        self.bid = random.randint(0, 999)               # 3 digit
        self.mid = random.randint(99999999, 999999999)  # 9 digit


    def confBrite(self, inter_bw, n_as=None, n_leaf=None, prefix='TD_conf'):
        # set Brite conf file 
        n_as = n_as if n_as else 2
        n_leaf = n_leaf if n_leaf else 32
        bname = '%s_%s.conf' % (prefix, self.bid)
        self.brt.setBwInter(inter_bw)
        self.brt.setAsNum(n_as)
        self.brt.setRouterNum(n_leaf)
        self.brt_path = self.brt.generate(bname)


    def getTruth(self, nums, rates, inter_bw, edge_bw):
        # given a certain setting dict, calculate ground truth
        # but actually I don't need downstream, cause I have edge bw to set...
        # TODO: change in brite-for-all.cc the downstream rx leaf, make it only 
        # go through edge
        n_normal, n_cross, n_ds = nums
        n_rate, c_rate, ds_rate = rates
        if n_ds:
            print('-> Warning: number of downstream flow for each edge isn\'t 0!')
        
        ds_crate = max(edge_bw - n_ds * ds_rate, edge_bw / (n_ds + 1))
        desire_rate = min(n_rate, ds_crate)
        desire_crate = min(c_rate, edge_bw)
        is_co = (desire_rate * n_normal + desire_crate * n_cross) > inter_bw

        return is_co


    def runNs3(self, mid, tid, nums, rates, edge_bw, bpath = None, \
        program='brite-for-all', tStop=30):
        # run brite_for_all
        bpath = bpath if bpath else self.brt_path
        os.chdir(self.ns3_path)
        args = {'mid':mid, 'tid':tid, 'nNormal':nums[0], 'nCross':nums[1], 
                'nDsForEach':nums[2], 'normalRate':rates[0], 'crossRate':rates[1], 
                'dsCrossRate':rates[2], 'edgeRate':edge_bw, 'tStop':tStop,
                'confFile':bpath}
        arg_str = ''
        for k in args:
            arg_str += ' -%s=%s' % (k, args[k])
        cmd = './waf --run "scratch/%s %s" > log_brite_%s.txt 2>&1' % \
            (program, arg_str, mid)
        print(cmd, '\n')
        os.system(cmd)


    def top(self):
        # call the tools above to simulate
        
        n_leaf = 32
        edge_bws = [50, 100, 500]
        inter_bws = [300, 600, 900]
        n_rate, c_rate = 100, 1000
        # N_range: expected 2,4,6

        for n_flow in range(self.N_min, self.N_max + 1):

            for inter_bw in inter_bws:
                self.confBrite(inter_bw, n_leaf=n_leaf)
                # self.confBrite(inter_bw, n_leaf=8*n_flow)       # adaptive?

                for edge_bw in edge_bws:

                    for n_cross in [0, n_flow]:
                        nums = [n_flow, n_cross, 0]
                        rates = [n_rate, c_rate, 0]
                        is_co = self.getTruth(nums, rates, inter_bw, edge_bw)
                        os.chdir(self.ns3_path)
                        self.runNs3(self.mid, self.bid % 20, nums, rates, edge_bw)

                        ftruth = 'MboxStatistics/bottleneck_%s.txt' % self.mid
                        with open(ftruth, 'a') as f:
                            f.write('%s %s' % (self.mid, is_co))
                        self.mid += 1

                self.bid += 1