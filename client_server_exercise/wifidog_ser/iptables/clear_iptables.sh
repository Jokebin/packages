#!/bin/sh

echo '###################################################################################'
iptables -F -t mangle
iptables -X -t mangle
iptables -F -t nat
iptables -X -t nat
iptables -F -t filter
iptables -X -t filter
iptables -t mangle -F outgoing
iptables -t mangle -F incoming
iptables -t mangle -F trust
iptables -t mangle -X outgoing 
iptables -t mangle -X incoming
iptables -t mangle -X trust

sleep 1
iptables -t mangle -nvL
echo '###################################################################################'
iptables --list-rules -t mangle
echo '###################################################################################'

iptables -t nat -F outgoing
iptables -t nat -F wifi2rout
iptables -t nat -F wifi2int
iptables -t nat -F global
iptables -t nat -F unknown
iptables -t nat -F authservers
iptables -t nat -X outgoing
iptables -t nat -X wifi2rout
iptables -t nat -X wifi2int
iptables -t nat -X global
iptables -t nat -X unknown
iptables -t nat -X authservers

sleep 1
iptables -t nat -nvL
echo '###################################################################################'
iptables --list-rules -t nat
echo '###################################################################################'

iptables -t filter -F wifi2int
iptables -t filter -F authservers
iptables -t filter -F locked
iptables -t filter -F global
iptables -t filter -F validate
iptables -t filter -F known
iptables -t filter -F unknown
iptables -t filter -X wifi2int
iptables -t filter -X authservers
iptables -t filter -X locked
iptables -t filter -X global
iptables -t filter -X validate
iptables -t filter -X known
iptables -t filter -X unknown

sleep 1
iptables -nvL
echo '###################################################################################'
iptables --list-rules -t filter
echo '###################################################################################'
