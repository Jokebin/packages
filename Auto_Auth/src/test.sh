#!/bin/sh

#set -x

ip_start=254
ip_prefix="172.30.22"

build(){
	local i=1
	local ip_suffix=$ip_start 
	while [ $i -le $1 ]
	do
		ip link	add link eth0 vth$i type macvlan
		ifconfig vth$i up
		ifconfig vth$i $ip_prefix.$ip_suffix
		ip route del 172.30.0.0/16
		i=$(( $i + 1 ))
		ip_suffix=$(( $ip_suffix - 1 ))
	done
}

clean(){
	local i=1
	while [ $i -le $1 ]
	do
		ip link del vth$i
		i=$(( $i + 1 ))
	done
}

case $1 in
set)
	shift
	build $1
	exit 0
	;;
del)
	shift
	clean $1
	exit 0
	;;
*)
	echo "please input the option"
	echo "$0 set|del"
	exit 1
	;;
esac
#esit 0

