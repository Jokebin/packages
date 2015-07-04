#!/bin/sh

echo '###################################################################################'
iptables -F -t mangle
iptables -X -t mangle
iptables -F -t nat
iptables -X -t nat
iptables -F -t filter
iptables -X -t filter

iptables -t mangle -N outgoing
iptables -t mangle -N incoming
iptables -t mangle -N trust

iptables -t mangle -I PREROUTING 1 -i br-lan -j outgoing
iptables -t mangle -I PREROUTING 1 -i br-lan -j trust
iptables -t mangle -I POSTROUTING 1 -o br-lan -j incoming

sleep 1
iptables -t mangle -nvL
echo '###################################################################################'
iptables --list-rules -t mangle
echo '###################################################################################'

iptables -t nat -N outgoing
iptables -t nat -N wifi2rout
iptables -t nat -N wifi2int
iptables -t nat -N global
iptables -t nat -N unknown
iptables -t nat -N authservers

iptables -t nat -A PREROUTING -i br_lan -j outgoing
iptables -t nat -A outgoing -d 192.168.2.1 -j wifi2rout
iptables -t nat -A wifi2rout -j ACCEPT
iptables -t nat -A outgoing -j wifi2int

iptables -t nat -A wifi2int -m mark --mark 0x2 -j ACCEPT
iptables -t nat -A wifi2int -m mark --mark 0x1 -j ACCEPT
iptables -t nat -A wifi2int -j unknown

#iptables -t nat -A unknown -j authservers
#iptables -t nat -A unknown -j global
iptables -t nat -A unknown -p tcp --dport 80 -j REDIRECT --to-ports 2060

sleep 1
iptables -t nat -nvL
echo '###################################################################################'
iptables --list-rules -t nat
echo '###################################################################################'

iptables -t filter -N wifi2int
iptables -t filter -N authservers
iptables -t filter -N locked
iptables -t filter -N global
iptables -t filter -N validate
iptables -t filter -N known
iptables -t filter -N unknown

iptables -t filter -I FORWARD -i br-lan -j wifi2int
iptables -t filter -A wifi2int -m state --state INVALID -j DROP
iptables -t filter -A wifi2int -o eth1 -p tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu
iptables -t filter -A wifi2int  -j authservers
iptables -t filter -A wifi2int  -m mark --mark 0x254 -j locked

#iptables_load_ruleset("filter", "locked-users", TABLE_WIFIDOG_LOCKED);
iptables -t filter -A locked -d 0.0.0.0/0 -j REJECT
iptables -t filter -A wifi2int -j global

iptables -t filter -A wifi2int -m mark --mark 0x1 -j validate
#iptables_load_ruleset("filter", "validating-users", TABLE_WIFIDOG_VALIDATE);
iptables -t filter -A validate -d 0.0.0.0/0 -j ACCEPT

iptables -t filter -A wifi2int -m mark --mark 0x2 -j known
#iptables_load_ruleset("filter", "known-users", TABLE_WIFIDOG_KNOWN)
iptables -t filter -A known -d 0.0.0.0/0 -j ACCEPT

iptables -t filter -A wifi2int -j unknown

iptables -t filter -A unknown -d 0.0.0.0/0 -p udp --dport 53 -j ACCEPT
iptables -t filter -A unknown -d 0.0.0.0/0 -p tcp --dport 53 -j ACCEPT
iptables -t filter -A unknown -d 0.0.0.0/0 -p udp --dport 67 -j ACCEPT
iptables -t filter -A unknown -d 0.0.0.0/0 -p tcp --dport 67 -j ACCEPT
iptables -t filter -A unknown -j REJECT --reject-with icmp-port-unreachable

sleep 1
iptables -t filter -nvL
echo '###################################################################################'
iptables --list-rules -t filter
echo '###################################################################################'
