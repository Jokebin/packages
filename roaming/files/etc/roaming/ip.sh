#!/bin/sh
#set -x

LOGGER="/usr/bin/logger -t ROAMING_UPDATE_IP"
LOCK=/tmp/run/ip.lock

clidir=/tmp/roaming/clients
tmpdir=/tmp/roaming/tmpcli
olderdir=/tmp/roaming/older_clients
local_dhcp=/tmp/dhcp.leases
remote_dhcp=/tmp/dhcp.hosts_remote

updateclients() {
	if [ ! -d $tmpdir ]; then
		mkdir -p $tmpdir
	fi

	if [ -f $local_dhcp ];then
		cat $local_dhcp | while read line
		do
			cmac=$(echo $line|awk '{print $2}')
			cip=$(echo $line|awk '{print $3}')

			cmac=${cmac//:/}
			echo "ip:$cip" > $tmpdir/$cmac
		done
	fi

	if [ -f $remote_dhcp ];then
		cat $remote_dhcp | while read line
		do
			cmac=$(echo $line|awk -F',' '{print $1}')
			cip=$(echo $line|awk -F',' '{print $3}')
			cmac=${cmac//:/}
			echo "ip:$cip" > $tmpdir/$cmac
		done
	fi
}

if [ ! -d "$clidir" ];then
	mkdir -p $clidir
fi

if [ ! -d "$olderdir" ];then
	mkdir -p $olderdir
fi
 
old_clients="$(ls $clidir)"
updateclients
cur_clients="$(ls $tmpdir)"

# check if clients update
if [ "$old_clients" != "$cur_clients" ]; then
	rm $clidir -rf
	mv $tmpdir $clidir
else
	rm $tmpdir -rf
fi
