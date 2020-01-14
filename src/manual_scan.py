import os
import subprocess as sp

topo_path = '/home/sapphire/scpt/TopoSurfer/xml'
py = '/home/sapphire/scpt/ConfluentSim/confluentSim.py'
content = sp.getoutput('ls %s' % topo_path).strip().split('\n')
prefix = ['Belnet', 'Garr1999', 'HibernialNireland', 'Sanren']
for f in content:
	for p in prefix:
		if f.find(p) == -1:
			continue
		path = os.path.join(topo_path, f)
		cmd = 'python3 %s -r 5:6 -x %s' % (py, path)
		print(cmd)
		os.system(cmd)
		break

