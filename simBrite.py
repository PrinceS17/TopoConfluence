'''
This script is used to simulate among different Brite topologies and scan 
parameters like the number target flows. Each run consists of these settings:

1. Specify the topology by determining Brite configure file (number of 
    leaf nodes, BW), and tid;
2. Specify the network parameters in ns-3, such as nNormal, nCross, etc;


TODO Things that need attention
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
        with open(self.path, 'r') as f:
            self.conf = f.read()
        
    def setBwInter(self, bw):
        self.conf = self.conf.replace('BWInterMin = 100.0', 'BWInterMin = %s' % bw)
        self.conf = self.conf.replace('BWInterMax = 1024.0', 'BWInterMax = %s' % bw)
    
    def setBwIntra(self, bw):
        self.conf = self.conf.replace('BWIntraMin = 1000.0', 'BWIntraMin = %s' % bw)
        self.conf = self.conf.replace('BWIntraMax = 4024.0', 'BWIntraMax = %s' % bw)
    
    def setAsNum(self, n):
        self.conf = self.conf.replace('N = 2', 'N = %s' % n)
    
    def setRouterNum(self, n):
        self.conf = self.conf.replace('N = 32', 'N = %s' % n)
    
    def generate(self, name):
        out_path = os.path.join(self.folder, name)
        with open(out_path, 'w') as f:
            f.write(self.conf)
        with open(self.path, 'r') as f:
            self.conf = f.read()            # recover the conf back to default
        return out_path


class simBrite:
    def __init__(self,
        N_range,
        bid=None,
        sample=None,
        root_folder=None,
        ns3_path=None
        ):
        self.N_min, self.N_max = N_range
        self.sample = sample if sample else 'TD_DefaultWaxman.conf'
        if root_folder:
            os.chdir(root_folder)
        rname = 'Brite_%s:%s_%s' % (N_range[0], N_range[1], \
            time.strftime("%b-%d-%H:%M:%S"))
        os.mkdir(rname)
        self.root = os.path.join(os.getcwd(), rname)
        self.ns3_path = ns3_path if ns3_path else os.path.join(os.getcwd(), \
            'ns-3-sim', 'ns-3.27')
        os.chdir(rname)
        os.mkdir('dat')
        os.mkdir('log')
        self.brt = Briter(os.path.join(self.ns3_path, 'brite_conf'))
        self.bid = bid if bid else random.randint(0, 999)
        self.mid = random.randint(99999999, 999999999)  # 9 digit
        os.system('touch mid.txt')
        self.mpath = os.path.join(self.root, 'mid.txt')


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

        return int(is_co)


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
            arg_str += '-%s=%s ' % (k, args[k])
        cmd = './waf --run "scratch/%s %s" > log_brite_%s.txt 2>&1' % \
            (program, arg_str, mid)
        print(cmd, '\n')
        os.system(cmd)
        os.chdir(self.root)
        cp_cmd = 'cp %s/*%s*dat dat' % (os.path.join(self.ns3_path, 'MboxStatistics'), mid)
        cp2_cmd = 'cp %s/log_brite_%s.txt log' % (self.ns3_path, mid)
        os.system(cp2_cmd)
        res = sp.getoutput(cp_cmd)
        os.chdir(self.ns3_path)
        if res.find('cp: cannot stat ') != -1:
            return False
        return True


    def loopRun(self, mid, bid, nums, rates, edge_bw, brt_path, \
        is_co, n_leaf, tStop=30, lock=None):
        # loop run ns-3 as a serial procedure, for multiprocessing
        k = 2
        path = brt_path
        if lock:
            lock.acquire()
        print('--> Loop run %s/18: mid = %s' % (self.cnt, mid))
        if lock:
            lock.release()

        while not self.runNs3(mid, bid, nums, rates, \
            edge_bw, bpath=path, tStop=tStop):
            print('--> Insufficient leaves: use k = %s' % k)
            self.brt.setRouterNum(n_leaf * k)
            path = self.brt.generate('LeavesAdded_%s.conf' % mid)      # test needed
            k += 1
            if k > 4:
                print('--> Leave number is too large! Break loop.')
                break
        
        ftruth = 'MboxStatistics/bottleneck_%s.txt' % mid
        with open(ftruth, 'w') as f:
            for i in range(nums[0]):
                f.write('%s %s\n' % (i, is_co - 1))
        print('mid path: %s' % self.mpath)
        with open(self.mpath, 'a') as f:
            f.write('%s %s %s\n' % (mid, nums[0], nums[1]))
        


    def top(self, tStop=30, segment=None):
        # call the tools above to simulate
        
        segment = segment if segment else 2
        n_leaf = 32
        edge_bws = [50, 100, 500]
        inter_bws = [300, 600, 900]
        n_rate, c_rate = 100, 1000
        # N_range: expected 2,4,6

        lock = Lock()
        procs, p_tmp = [], []
        self.cnt = 0
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

                        p = Process(target=self.loopRun, args=(self.mid, self.bid, \
                            nums, rates, edge_bw, self.brt_path, is_co, n_leaf, tStop, lock))
                        procs.append(p)
                        self.mid += 1
                self.bid += 1

        for p in procs:
            p_tmp.append(p)
            self.cnt += 1
            p.start()
            if len(p_tmp) == segment:
                for q in p_tmp:
                    q.join()
                p_tmp = []
            time.sleep(15)


def test_briter():
    assert 'ns-3-sim' in test_ls()
    folder = os.path.join(os.getcwd(), 'ns-3-sim', 'ns-3.27', 'brite_conf')
    brt = Briter(folder)
    assert 'TD_DefaultWaxman.conf' in test_ls(folder)
    assert brt.path.find('TD_DefaultWaxman.conf') != -1
    brt.setBwInter(1000)
    brt.setBwIntra(999)
    brt.setAsNum(3)
    brt.setRouterNum(20)
    brt.generate('TD_test.conf')
    assert 'TD_test.conf' in test_ls(folder)
    with open(os.path.join(folder, 'TD_test.conf'), 'r') as f:
        tmp = f.read()
        assert tmp.find('BWInterMin = 1000') != -1
        assert tmp.find('BWIntraMin = 999') != -1
        assert tmp.find('N = 3') != -1
        assert tmp.find('N = 20') != -1
    brt.setAsNum(4)
    brt.generate('TD_test2.conf')
    assert 'TD_test2.conf' in test_ls(folder)
    with open(os.path.join(folder, 'TD_test2.conf'), 'r') as f:
        tmp = f.read()
        assert tmp.find('N = 4') != -1      # test we can set AS num again
    
    print('- Briter test passed!')


def test_simBrite_unit():
    # test tool functions of simBrite
    tsb = simBrite([1,2])
    assert tsb.root.find('Brite_1:2') != -1
    assert os.getcwd().find('Brite_1:2') != -1    # should in Brite root now
    print('- Root test passed.')

    tsb.confBrite(1024, 5, 48)
    bname = 'TD_conf_%s.conf' % tsb.bid
    conf_path = os.path.join(tsb.ns3_path, 'brite_conf')
    assert bname in test_ls(conf_path)
    with open(os.path.join(conf_path, bname)) as f:
        tmp = f.read()
    assert tmp.find('BWInterMin = 1024') != -1
    assert tmp.find('N = 5') != -1
    print('- Brite conf test passed.')

    is_co = tsb.getTruth([2,0,2], [5,10,3], 10, 10)
    assert not is_co 
    is_co = tsb.getTruth([4,1,0], [2,4,6], 10, 5)
    assert is_co
    print('- Get truth test passed.')

    np = Process(target=tsb.runNs3, args=(1111, tsb.bid % 10, [4,1,0],\
        [200,400,600], 500,))
    print('- Running ns-3 ...')
    np.start()
    time.sleep(15)
    assert 'log_brite_1111.txt' in test_ls(tsb.ns3_path)
    np.terminate()
    ans = input('- Is the command above correct? (y/n) ')
    if ans == 'y':
        print('- NS test passed.')
    else:
        print('- NS test failed! Exit.')
        exit(1)


def test_simBrite_int():
    # test top() of simBrite
    if os.getcwd()[-7:] != 'fluence':
        os.chdir('..')
        assert os.getcwd()[-7:] == 'fluence'
    tsi = simBrite([1,2])
    mid = tsi.mid
    np = Process(target=tsi.top, args=(1,))
    np.start()
    print('- Scanning the cases...')
    np.join()

    dat_path = os.path.join(tsi.ns3_path, 'MboxStatistics')
    assert 'bottleneck_%s.txt' % mid in test_ls(dat_path)
    os.chdir(tsi.root)
    assert 'AckLatency_%s_0.dat' % mid in test_ls('dat')
    assert 'log_brite_%s.txt' % mid in test_ls('log')

    ans = input('- Is the running process normal? (y/n) ')
    if ans == 'y':
        print('- Top test passed.')
    else:
        print('- Top test failed! Exit.')
        exit(1)


def print_help():
    print('Usage: python %s -r MIN:MAX -b BRITE_RANDOM_ID -c CORE_NUM \
        [-t TIME_DURATION]' % sys.argv[0])
    print('              [-s SAMPLE] [-R ROOT_FOLDER] [-n NS3_PATH]')
    exit(1)

if __name__ == "__main__":
    is_test = False

    if is_test:
        print('In test mode, all arguments ignored.')
        test_briter()
        # test_simBrite_unit()
        test_simBrite_int()
        exit(0)

    if len(sys.argv) < 2:
        print_help()
    
    opt_map = {'-r':'3:4', '-s':None, '-R':None, '-n':None, '-t':30, \
        '-b':None, '-c':2}
    cur_arg = None
    for arg in sys.argv[1:]:
        if arg in opt_map:
            cur_arg = arg
        elif cur_arg in opt_map:
            opt_map[cur_arg] = arg
            cur_arg = None
        else:
            print('No such options! Exit.')
            exit(1)
    
    num = [int(n) for n in opt_map['-r'].split(':')]
    tStop = int(opt_map['-t'])
    simb = simBrite(num, int(opt_map['-b']), opt_map['-s'], opt_map['-R'], opt_map['-n'])
    simb.top(segment=int(opt_map['-c']))

