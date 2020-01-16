'''
This script is used to scan all the combinations of target flow, cross 
traffic and target edge bandwidth, get ground truth, and record data, 
given the XML topologe file which can be generated by topo-parse or 
topoSurfer, and the range of number of flows [N_min, N_max]. It will:

1. Generate <src, des> for each N in the range;
2. For each <src, des>, draw each leaf's BW from a set of distributions;
3. For each (<src, des>, leaf_bws), generate cross traffic <csrc, cdes>;
4. For each (<src, des>, leaf_bws, <csrc, cdes>), run ns-3.

Note that ground truth of co-bottleneck can be obtained by computing 
link capacity and routing path inside ns-3 and will be output to file.

Format information:
Key arguments for ns-3: mid, nFlow, mode(4.), flowInfo(1.2.3), topo (xml)
Flow info name in info/: flow_info_MID.txt
File needed by 4.: flow_rate_mode=0_MID.dat, flow_rate_mode=1_MID.dat
- Format: one flow rate per line

Result files: *_MID_x.dat, including bottleneck_MID.dat (ground truth file)
Root directory:
- Flow info -> MID: structure.json


History:                1.8.2020        Test code added.
                        1.10.2020       Mutiprocessing added.
                        1.13.2020       Polish with ns-3 ground truth.

'''

from matplotlib import pyplot as plt
from multiprocessing import Process, Lock
import os, sys, time, random, json
import numpy as np
import subprocess as sp
import xmltodict as xd
from find_cluster_test import find_cluster  # given raw rates of absent mode, return flow co-bottleneck

is_test = False


class leafRng:
    # generate rng instances for leaf bandwidth
    def __init__(self, mu, sigma):
        # currently Normal distribution, because the leaf is the collection
        # of a great number of edge links
        self.mu, self.sigma = mu, sigma

    def getNormal(self, N=1):
        # deprecated: get some new random value
        res = list(np.random.normal(self.mu, self.sigma, N))
        for i in range(len(res)):
            while res[i] < 0:
                res[i] = np.random.normal(self.mu, self.sigma)
        return res
    
    def get(self, N=1):
        # log normal distribution, [min, max] = [10k, 10G]
        # log(10M) = 16.1, log(1G) = 20.7
        res = list(np.random.lognormal(self.mu, self.sigma, N))
        for i in range(len(res)):
            while res[i] < 10e3 or res[i] > 10e9:
                res[i] = np.random.lognormal(self.mu, self.sigma)
        return res


class confluentSim:
    def __init__(self,
        N_range,                     # target flow range: N_min, N_max
        N_node,                     # total number of node
        xml_path,                   # path of XML topology file
        root_folder=None,
        ns3_path=None,
        rate_path=None,             # folder where flow_rate is
        ):

        self.N_min, self.N_max = N_range
        self.N_node = N_node                   
        self.xml_path = xml_path
        if root_folder:
            os.chdir(root_folder)
        self.ns3_path = ns3_path if ns3_path else os.path.join(os.getcwd(), 'ns-3-sim', 'ns-3.27')
        self.rate_path = rate_path if rate_path else os.path.join(self.ns3_path, 'MboxStatistics')

        # rname = 'Flows_' + + time.strftime("_%b-%d-%H:%M:%S")
        pos = xml_path.find('xml/')
        rname = 'Flows_%s:%s_%s_%s' % (self.N_min, self.N_max, xml_path[pos + 4:], \
            time.strftime("_%b-%d-%H:%M:%S"))
        os.mkdir(rname)
        self.root = os.path.join(os.getcwd(), rname)
        os.chdir(rname)
        os.mkdir('info')            # generated flow info file for ns-3
        os.mkdir('dat')             # collected results
        os.mkdir('log')             # collected logs

        self.cobottleneck_th = 0.9  # if new_sum_rate < th * old_sum_rate, mark co-bottleneck
        self.mid = random.randint(1000, 9999)
        self.cnt, self.N_run = 0, 0
    

    def create_rngs(self):
        # generate a set of leaf rngs for leaf bandwidth
        # means = list(range(200, 801, 300))       # deprecated, unit in Mbps for Normal
        means = [np.log(250e6), np.log(500e6), np.log(1e9)]     # unit in log(bps), for lognormal
        dev = np.log(10)
        self.rngs = []
        for i in range(len(means)):
            rng = leafRng(means[i], dev)
            self.rngs.append(rng)


    def gen_target_flow(self, N, N_node):
        # (sub) generate target flow <src, des> pairs for given N (1.)
        candidate = list(range(N_node))
        src = random.sample(candidate, 1)
        candidate.remove(src[0])
        des = random.sample(candidate, N)
        return list(zip(src * N, des))


    def draw_leaf_bw(self, flows, ri):
        # (sub) generate leaf bw for each flow <src, des>, with given rng
        return self.rngs[ri].get(len(flows))


    def gen_cross_traffic(self, N):
        # (sub) generate randomly N cross traffic <src, des>
        src = random.sample(range(self.N_node), N)
        des = random.sample(range(self.N_node), N)
        for i in range(N):
            while src[i] == des[i]:
                des[i] = random.sample(range(self.N_node), 1)[0]
        return list(zip(src, des))


    def write_flow_info(self, flows, leaf_bw, cross_traffic, mid, prefix='flow_info'):
        # (tool) given all the data for a double-run, write info flow-info
        fname = prefix + '_' + str(mid) + '.txt'
        fpath = os.path.join(self.root, 'info', fname)
        with open(fpath, 'w') as f:
            for flow, bw in zip(flows, leaf_bw):
                f.write('%s %s %s\n' % (flow[0], flow[1], bw))
            for src, des in cross_traffic:
                f.write('%s %s\n' % (src, des))
        
        return fpath


    # # deprecated!
    # def check_rates(self, mid, is_write=True, prefix='flow_rate'):
    #     # (tool) check the flow_rates file of run with mid, write to
    #     # a ground truth file, return if co-bottleneck
    #     # TODO: co-bottleneck only among partial flows in a single run?
    #     os.chdir(self.root)
    #     print('Rate file wanted: ')
    #     sum_rate = []
    #     for mode in [0, 1]:
    #         fname = '%s_mode=%s_%s.dat' % (prefix, mode, mid)
    #         print('- %s' % fname)
    #         with open(os.path.join(self.rate_path, fname), 'r') as f:
    #             s = f.read()
    #         rates = [float(r) for r in s.strip().split('\n')]
    #         sum_rate.append(sum(rates))
        
    #     if sum_rate[1] < self.cobottleneck_th * sum_rate[0]:
    #         is_cobottleneck = 1
    #     else:
    #         is_cobottleneck = 0
    #     with open(self.truth, 'a') as f:
    #         f.write('%s %s\n' % (mid, is_cobottleneck))
        
    #     return is_cobottleneck

    # deprecated!
    # def check_absent_rates(self, mid, N, is_write=True, prefix='flow_rate'):
    #     # get rates in all stages from the ns-3 run in absent mode, and check the co-bottlenecks
    #     os.chdir(self.root)
    #     print('Rate file wanted for absent mode:')
    #     fname1 = '%s_mode=1_%s.dat' % (prefix, mid)
    #     fnames, raw_rate = [], []
    #     print('- %s' % fname1)

    #     with open(os.path.join(self.rate_path, fname1), 'r') as f:
    #         s = f.read()
    #     ini_rates = [float(r) for r in s.strip().split('\n')]

    #     for i in range(N):
    #         name = '%s_mode=2_%s_%s.dat' % (prefix, mid, i)
    #         fnames.append(name)
    #         mat.append([])
    #         with open(os.path.join(self.rate_path, name), 'r') as f:
    #             s1 = f.read()
    #         rates = [float(r) for r in s1.strip().split('\n')]
    #         rates[i] = ini_rates[i]         # original value is 0 because of flow absence
    #         raw_rate.append(rates)
        
    #     is_co = find_cluster(raw_rate)
    #     with open(self.truth, 'a') as f:
    #         for flow, co_No in is_co:
    #             # Note: repeated co-btnk No. only makes sense with same MID!!!
    #             f.write('%s %s %s\n' % (mid, flow, co_No))
        
    #     return is_co


    def run_single(self, N_flow, mode, flow_info, mid, topo, program='confluence', tStop=30):
        # (tool) run single run of ns3, given mode and flow_info file
        # currently mixing in multi-processing context
        os.chdir(self.ns3_path)
        args = { 'mid':mid, 'nFlow':N_flow, 'flowInfo':flow_info, 'topo':topo, 'tStop':tStop}
        arg_str = ''
        for k in args:
            arg_str += ' -%s=%s' % (k, args[k])
        cmd = './waf --run "scratch/%s %s" > log_confluence_%s.txt 2>&1' % (program, arg_str, mid)
        print(cmd, '\n')
        os.system(cmd)


    def run_double(self, flows, leaf_bw, cross_traffic, mid, topo, l=None, single=True, tStop=30):
        # given all info generated, run ns-3 (call tools)
        if l:
            l.acquire()
        print(' - Double run %s/%s is running ... ' % (self.cnt, self.N_run))
        N_flow = len(flows)
        flow_info = self.write_flow_info(flows, leaf_bw, cross_traffic, mid)
        if l:
            l.release()
        
        self.run_single(N_flow, 1, flow_info, mid, topo, tStop=tStop)
        cres = self.collect_result(mid)            # execute here to follow internal-process dependency
        if cres:
            return
        if l:
            l.acquire()
        c = input('==>> ns-3 isn\'t healthy now! Do you want to continue (otherwise stop) ? (y/n)')
        if c != 'y':
            exit(1)
        if l:
            l.release()

                
    def collect_result(self, mid):
        # get result data file from ns-3 and store with ground truth file
        # store all raw data with mid in dat, but generate json specifying the
        # structure of the confluence, i.e. flows, leaf_bw, into a big dict        
        os.chdir(self.root)
        
        out = sp.getoutput('cp %s/*%s*dat dat' % (self.rate_path, mid))
        os.system('cp %s/log_confluence_%s.txt log' % (self.ns3_path, mid))
        if out.find('cp: cannot stat') != -1:
            return False
        return True

    
    def top(self, dry_run=False, multi_proc=True, capping=False, segment=4, tStop=30):
        # call all above and get the things done
        # generate clear dict for all parameters, then export json to infer
        # corresponding MIDs
        
        self.create_rngs()
        self.structure = {}
        self.M = 3                  # number of random flows for each N
        self.T_cap = 18000          # max runtime: 5h

        # TODO: think: generation of cross number N??
        # self.C = 20                 # number of cross traffic
        self.C = self.N_node // 3         
        # self.C = self.N_node ** 2

        print('Range of N (number of target flows): %s ~ %s' % (self.N_min, self.N_max))
        print('# Random sets for each N: %s' % self.M)
        print('# Edge BW distribution for each set: %s' % len(self.rngs))
        for rng in self.rngs:
            print('  - log noraml, mean: %s, stddev: %s' % (rng.mu, rng.sigma))
        self.N_run = (self.N_max - self.N_min + 1) * self.M * len(self.rngs)
        print('-> Total number of double runs: %s\n' % self.N_run)
        
        os.chdir(self.ns3_path)
        os.system('./waf build')

        T_total = 0
        p_tmp = []
        
        for N in range(self.N_min, self.N_max + 1):
            self.structure[N] = {}
            procs = []          # multiprocessing all M * len(rngs) for each N
            lock = Lock()
            for m in range(self.M):
                flows = self.gen_target_flow(N, self.N_node)

                self.structure[N][m] = {}
                self.structure[N][m]['flows'] = flows
                self.structure[N][m]['data'] = {}

   
                for ri in range(len(self.rngs)):
                    leaf_bw = self.draw_leaf_bw(flows, ri)
                    cross_traffic = self.gen_cross_traffic(self.C)
                    mid = self.mid
                    self.mid = self.mid % 9999 + 1
                    if not dry_run and not multi_proc:
                        self.run_double(flows, leaf_bw, cross_traffic, mid, self.xml_path)
                    elif not dry_run:
                        p = Process(target=self.run_double, args=(flows, leaf_bw, cross_traffic, \
                            mid, self.xml_path, lock, True, tStop))
                        procs.append(p)

                    self.structure[N][m]['data'][ri] = {}
                    rstruct = self.structure[N][m]['data'][ri]
                    rstruct['leaf bw'] = leaf_bw
                    rstruct['cross traffic'] = cross_traffic
                    rstruct['mid'] = mid
                
            # segmented multiprocessing
            for p in procs:
                p_tmp.append(p)
                self.cnt += 1
                p.start()
                if len(p_tmp) == segment:
                    for q in p_tmp:
                        if T_total >= self.T_cap and capping:
                            q.terminate()
                            continue
                        while q.is_alive():
                            q.join(60)
                            T_total += 60
                    p_tmp = []
                    T_total = 0
                time.sleep(30)              # magic, but works

            # all in one multiprocessing
            # for p in procs:
            #     self.cnt += 1
            #     p.start()
            # for p in procs:
            #     if T_total >= self.T_cap and capping:     # time management to avoid too long running
            #         p.terminate()
            #         continue
            #     while p.join(60) is None:
            #         T_total += 60
                
        os.chdir(self.root)
        with open('structure.json', 'w') as f:
            s = json.dumps(self.structure, sort_keys=True)
            f.write(s)


def print_help():
    help_msg = 'Usage: python3 %s -r MIN:MAX -c PROCESS_NUM -x XML_FILE_PATH \
        -n NS3_PATH -t TIME_DURATION -h' % sys.argv[0]
    print(help_msg)
    exit(1)


def get_num_from_xml(xml_path):
    # parse xml to get the number of node in total, tested
    with open(xml_path, 'r') as f:
        xml = f.read()
    xdict = xd.parse(xml)
    return len(xdict['topology']['node'])


def test_ls(path=None):
    if path:
        os.chdir(path)
    tmp = os.popen('ls').read()
    tlist = tmp.strip().split('\n')
    return tlist


def test_rng():
    # logNormal distribution test
    rng = leafRng(np.log(1e6), np.log(2))
    sample = rng.get(1000)
    assert abs(np.mean([np.log(s) for s in sample]) - np.log(1e6)) < 0.1
    assert abs(np.std([np.log(s) for s in sample]) - np.log(2)) < 0.05
    
    rng = leafRng(np.log(1e6), 100)
    sample = rng.get(100)
    for x in sample:
        assert 10e3 < x < 10e9


def test_confluence_unit(before_run = True):
    # test confluentSim class, w/o touching top
    xml = '/home/sapphire/scpt/tsurfer0/xml/Arpanet196912-5.0ms-bdp.xml'
    N_node = get_num_from_xml(xml)
    assert N_node == 4
    ctest = confluentSim([1,2], N_node, xml)
    os.chdir(ctest.root)
    content = test_ls()
    assert 'info' in content and 'dat' in content
    print('Test: mid = %s' % ctest.mid)

    # test create_rngs
    ctest.create_rngs()
    assert len(ctest.rngs) == 3 and ctest.rngs[0].sigma == np.log(10)

    # test gen_target_flow
    n = 2
    flows = ctest.gen_target_flow(n, ctest.N_node)
    assert len(flows) == n and flows[0][0] == flows[1][0]
    for src, des in flows:
        assert 0 <= src < ctest.N_node and 0 <= des < ctest.N_node
    
    # test draw_leaf_bw
    sflows = flows * 100
    sample = ctest.draw_leaf_bw(sflows, 1)
    assert abs(np.mean([np.log(s) for s in sample]) - ctest.rngs[1].mu) < 2

    # test gen_cross_traffic
    n = 2
    cross = ctest.gen_cross_traffic(n)
    assert len(cross) == n
    for src, des in cross:
        assert 0 <= src < ctest.N_node and 0 <= des < ctest.N_node
        assert src != des
    print('Sub functions test passed!')

    # test write_flow_info
    mid = 1234
    ctest.write_flow_info(flows, sample, cross, mid)
    os.chdir(os.path.join(ctest.root, 'info'))
    assert 'flow_info_1234.txt' in test_ls()
    with open('flow_info_1234.txt', 'r') as f:
        s = f.readline()
        assert s == '%s %s %s\n' % (flows[0][0], flows[0][1], sample[0])

    if before_run:
        print('All tests before ns-3 run passed!')
        return

    # test run and check rates
    ctest.run_double(flows, sample, cross, mid, xml)            # might not suitable for xml
    res = input('Is the ns-3 command correct? (y/n)')
    if res == 'y':
        print('ns-3 & flow info test passed.')
    else:
        print('ns-3 test failed!')
        exit(1)
    
    # rate = []
    # for mode in [0, 1]:
    #     fname = 'flow_rate_mode=%s_%s.dat' % (mode, mid)
    #     with open(os.path.join(ctest.rate_path, fname), 'r') as f:
    #         s = f.read().strip().split('\n')
    #     s1 = [float(r) for r in s]
    #     rate.append(sum(s1))
    # is_co = 1 if rate[1] < ctest.cobottleneck_th * rate[0] else 0
    # with open(ctest.truth, 'r') as f:
    #     data = f.readline().strip().split(' ')
    # assert int(data[0]) == mid and int(data[1]) == is_co

    # test collect_result
    ctest.collect_result(mid)
    os.chdir(ctest.root)
    assert 'AckLatency_%s_0.dat' % mid in test_ls('dat')
    print('All unit tests passed!')


def test_top():
    # integration test, check structure
    xml = '/home/sapphire/scpt/TopoSurfer/xml/Arpanet19706-5.0ms-bdp.xml'
    ttest = confluentSim([1,3], 9, xml)
    ttest.top(dry_run=False)
    os.chdir(ttest.root)
    with open('structure.json') as f:
        s = json.load(f)
    for n in range(1, 3):
        assert n in s
        assert 'flows' in s[n][0] and 'data' in s[n][ttest.M - 1]
    print(json.dumps(s, sort_keys=True, indent=4))
    res = input('Is the structure dictionary above correct? (y/n) ')
    if res == 'y':
        print('Structure test passed.')
    else:
        print('Structure test failed!')
        exit(1)
    
    # the data rate should be checked, but as a part of ns-3 unit test


if __name__ == "__main__":
    is_test = False

    if is_test:
        print('In test mode, arguments ignored.')
        test_rng()
        # test_confluence_unit(before_run=False)
        test_top()
        exit(0)

    if len(sys.argv) < 2:
        print_help()

    opt_map = {'-r':'1:2', '-c':4, '-x':None, '-n':None, '-t':30}
    cur_opt = None
    for arg in sys.argv[1:]:
        if arg == '-h':
            print_help()
        elif arg in opt_map:
            cur_opt = arg
        elif cur_opt in opt_map:
            opt_map[cur_opt] = arg
        else:
            print('Error: no this argument!')
            exit(1)
    
    xml_path = opt_map['-x']
    ns3_path = opt_map['-n']
    N_core = int(opt_map['-c'])
    N_range = [int(n) for n in opt_map['-r'].split(':')]
    N_node = get_num_from_xml(xml_path)
    tStop = int(opt_map['-t'])
    csim = confluentSim(N_range, N_node, xml_path, ns3_path=ns3_path)
    csim.top(segment=N_core, tStop=tStop)

