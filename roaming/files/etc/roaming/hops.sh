#!/bin/sh
#set -x

LOGGER="/usr/bin/logger -t ROAMING_HOPS"
ROAMING=/usr/sbin/roaming
LOCK=/tmp/run/hops.lock

hopdir=/tmp/roaming/hops
tmpdir=/tmp/roaming/tmp
olderdir=/tmp/roaming/older_router

MAX_HOPS=$(uci get roaming.config.hops)
if [ -z "$MAX_HOPS" ]; then
	MAX_HOPS=1;
fi

updatehops() {

	if [ ! -d "$tmpdir" ]; then
		mkdir -p $tmpdir
	fi

	if ip route |grep -q "default"; then
		localrouter_info=$(bmx6 -c status|tail -n1|awk '{print $3,$4}'|cut -d'-' -f2)
		localip=$(echo $localrouter_info|cut -d' ' -f2)
		localrouter=$($ROAMING -c ip6Tomac $localip)
		echo "ip6:$localip" > $tmpdir/$localrouter
		echo "hops:0" >> $tmpdir/$localrouter
	fi

	allrouters=$(bmx6 -c originators|awk '/fd66/{if($5 != "::"){print $1,$3}}')

	bmx6 -c tunnels|awk '{if($11=="0.0.0.0/0") print $8,$10}'|while read line
	do
		tunname=$(echo $line|cut -d' ' -f1)
		routername=$(echo $line|cut -d' ' -f2)
		routerinfo=${allrouters#*$routername}

		if [ "$routerinfo" == "$allrouters" -o -z "$routerinfo" ]; then
			$LOGGER "cannt get ip6addr for $routername"
			continue
		fi

		primaryIp=$(echo $routerinfo | cut -d ' ' -f1)
		if [ "${primaryIp#*:}" == "${primaryIp}" -o -z "${primaryIp##*:}" ]; then
			$LOGGER "Invalid primaryIp $primaryIp for $routername"
			continue
		fi
	
		routername=$($ROAMING -c ip6Tomac $primaryIp)
		if [ "$routername" == "Failed" -o "$routername" == "000000000000" ]; then
			continue
		fi


		hops=$(traceroute6 -w 10 $primaryIp 2>>/dev/null | tail -n1 | grep -v "!" | cut -d ' ' -f2)
		if [ -z "$hops" -o $hops -gt $MAX_HOPS ];then
			$LOGGER "To $routername<$primaryIp> over the limits: $MAX_HOPS"
			continue
		fi

		echo "ip6:$primaryIp" >$tmpdir/$routername

		if [ "$tunname" != "---" ];then
			# currently used wan
			echo "hops:-$hops" >> $tmpdir/$routername
		else
			echo "hops:$hops" >> $tmpdir/$routername
		fi
	done
}

if [ ! -d "$hopdir" ];then
	mkdir -p $hopdir
fi

if [ ! -d "$olderdir" ];then
	mkdir -p $olderdir
fi

old_routers="$(ls $hopdir)"
updatehops
cur_routers="$(ls $tmpdir)"

# check if routers update
for r in $old_routers
do
	if [ "${cur_routers#*$r}" == "$cur_routers" ]; then
		mv $hopdir/$r $olderdir/
	fi
done

# routers update
cp $tmpdir/* $hopdir/ -rf

rm $tmpdir -rf
