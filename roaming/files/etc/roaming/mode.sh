#!/bin/sh
#set -x
#set -e

LOGGER="/usr/bin/logger -t ZYC_IP_MODE" 

clean_nat_rules() {
	$LOGGER "change to gateway mode, clean nat rules"

	local lan_ip=$(uci get roaming.config.lanip)
	local mask=$(uci get roaming.config.netmask)

	[ -z "$lan_ip" ] && lan_ip=$(uci get network.lan.ipaddr)
	[ -z "$lan_ip" ] && return
	[ -z "$mask" ] && mask='12'

	local roaming_rules="iptables -w -t nat -I POSTROUTING -s ${lan_ip}/${mask} ! -d 10.0.0.0/8 -j ACCEPT"
	local clean_cmd="iptables -w -t nat -D POSTROUTING -s ${lan_ip}/${mask} ! -d 10.0.0.0/8 -j ACCEPT"
	local check_cmd="iptables -w -t nat -C POSTROUTING -s ${lan_ip}/${mask} ! -d 10.0.0.0/8 -j ACCEPT"

	if cat /etc/firewall.user|grep -q "$roaming_rules"; then
		eval "sed -e '/iptables \-w \-t nat \-I POSTROUTING \-s ${lan_ip}\/${mask} ! \-d 10.0.0.0\/8 \-j ACCEPT/d' -i /etc/firewall.user"
	fi

	if cat /etc/firewall.user.bak|grep -q "$clean_rules"; then
		eval "sed -e '/iptables \-w \-t nat \-D POSTROUTING \-s ${lan_ip}\/${mask} ! \-d 10.0.0.0\/8 \-j ACCEPT/d' -i /etc/firewall.user.bak"
	fi

	while $check_cmd; do
		$clean_cmd >>/dev/null 2>&1
	done
}

setup_nat_rules() {
	$LOGGER "change to client mode, setup nat rules"

	lan_ip=$(uci -q get roaming.config.lanip)
	mask=$(uci -q get roaming.config.netmask)

	[ -z "$lan_ip" ] && lan_ip=$(uci get network.lan.ipaddr)
	[ -z "$lan_ip" ] && return
	[ -z "$mask" ] && mask='12'

	local roaming_rules="iptables -w -t nat -I POSTROUTING -s ${lan_ip}/${mask} ! -d 10.0.0.0/8 -j ACCEPT"
	local clean_cmd="iptables -w -t nat -D POSTROUTING -s ${lan_ip}/${mask} ! -d 10.0.0.0/8 -j ACCEPT"
	local check_cmd="iptables -w -t nat -C POSTROUTING -s ${lan_ip}/${mask} ! -d 10.0.0.0/8 -j ACCEPT"

	#$clean_cmd >>/dev/null 2>&1
	$check_cmd || $roaming_rules
	if ! cat /etc/firewall.user|grep -q "$roaming_rules"; then
		echo "$roaming_rules" >>/etc/firewall.user
	fi
	
	if ! cat /etc/firewall.user.bak |grep -q "$clean_cmd"; then
		echo "$clean_cmd" >>/etc/firewall.user.bak
	fi
}

roaming_nat_rules() {
	gwflag=$(uci -q get roaming.config.gwnode)
	if [ $gwflag -eq 1 ]; then
		clean_nat_rules
	else
		setup_nat_rules
	fi
}

$@
