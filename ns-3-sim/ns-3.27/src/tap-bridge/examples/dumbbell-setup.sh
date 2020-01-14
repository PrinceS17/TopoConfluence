sudo brctl addbr br-right
sudo tunctl -t tap2
sudo ifconfig tap2 0.0.0.0 promisc up
sudo brctl addif br-right tap2
sudo ifconfig br-right up
sudo lxc-create -f lxc-right-ubuntu.conf -t download -n right -- -d ubuntu -r xenial -a amd64
sudo lxc-start -n right -d

