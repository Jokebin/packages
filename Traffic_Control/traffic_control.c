#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>

#include "traffic_control.h"
#include "util.h"

//tc_sys_conf info
tc_sys_conf sys_conf;

int ipnum = 0;
int classid = CLASSID_START;
int use_mark = MARK_START;
user_info *firstuser = NULL;

/*
 *clean tc rules
 * */
void tc_clean()
{
	classid = CLASSID_START;
	use_mark = MARK_START;
	ipnum = sys_conf.ip_end - sys_conf.ip_start + 1;
	tc_do_command("qdisc del dev %s root", sys_conf.download_if);
	tc_do_command("qdisc del dev %s root", sys_conf.upload_if);
	iptables_fw_destroy_mention("mangle", "POSTROUTING", DOWNLOAD_IPTABLE);
	iptables_fw_destroy_mention("mangle", "POSTROUTING", UPLOAD_IPTABLE);
	iptables_do_command("-t mangle -F " DOWNLOAD_IPTABLE);
	iptables_do_command("-t mangle -F " UPLOAD_IPTABLE);
	iptables_do_command("-t mangle -X " DOWNLOAD_IPTABLE);
	iptables_do_command("-t mangle -X " UPLOAD_IPTABLE);
	execute("rmmod sch_htb 2>/dev/null", 0);
}

/**
 *traffic control reset
 *insmod sch_htb and clear existed qdisc
 */
int tc_reset()
{
	tc_clean();
	execute("insmod sch_htb 2>/dev/null", 0);	
	iptables_do_command("-t mangle -N " DOWNLOAD_IPTABLE);
	iptables_do_command("-t mangle -N " UPLOAD_IPTABLE);
	iptables_do_command("-t mangle -I POSTROUTING 1 -o %s -j" UPLOAD_IPTABLE, sys_conf.upload_if);
	iptables_do_command("-t mangle -I POSTROUTING 1 -o %s -j" DOWNLOAD_IPTABLE, sys_conf.download_if);
}

/*
 *tc_test if a ip is used
 * */
user_info* if_ip_used(const char *ipaddr)
{
	user_info *ptr = firstuser;
	addr_node *addr = NULL;
	while(ptr)
	{
		if(GET_BIT(ptr->ipmask, get_ip_posfix(ipaddr) - sys_conf.ip_start))
		{
			return ptr;
		}
		ptr = ptr->next;
	}
	return NULL;
}

int add_addr_node(user_info *user, char *ipaddr, char *mac)
{
	addr_node *paddr = malloc(sizeof(addr_node));
	if(paddr == NULL)
	{
		printf("<error> no memeroy left\n");
		return -1;
	}
	bzero(paddr, sizeof(addr_node));
	strcpy(paddr->ipaddr, ipaddr);
	if(!mac)
		strcpy(paddr->mac, DEFAULT_MAC);
	else
		strcpy(paddr->mac, mac);
	paddr->next = user->addr;
	user->addr = paddr;

	return 0;
}

/*
 *set tc conf info
 *return value 0:sucess -1:failed
 *confi_info format:
 *download_if,upload_if,invalid_rate,gateway_ip_prefix,owner_user,ip_start,ip_end
 * */
int tc_set_conf(char *conf_info)
{
	int ret = 0;
	char buf[MAX_BUF];	

	bzero(&sys_conf, sizeof(tc_sys_conf));
	if(conf_info)
	{
		ret |= sscanf(conf_info, "%[^,],%[^,],%[^,],%[^,],%[^,],%d,%d", sys_conf.download_if, 
					  sys_conf.upload_if, sys_conf.invalid_rate, sys_conf.gateway_ip_prefix,
					  sys_conf.owner_user, (int*)&sys_conf.ip_start, (int*)&sys_conf.ip_end);
	} else {
		bzero(buf, sizeof(buf));
		//format:download_if,upload_if,invalid_rate,gateway_ip_prefix,owner_user,ip_start,ip_end 
		ret |= sprintf(buf, "%s,%s,%s,%s,%s,%d,%d", DEFAULT_DOWNLOAD_IF, DEFAULT_UPLOAD_IF, 
					  DEFAULT_INVALID_RATE, DEFAULT_GATEWAY_IP_PREFIX, DEFAULT_OWNER_USER, 		 
					  DEFAULT_IP_START, DEFAULT_IP_END);
		ret |= sscanf(buf, "%[^,],%[^,],%[^,],%[^,],%[^,],%d,%d", sys_conf.download_if, 
					  sys_conf.upload_if, sys_conf.invalid_rate, sys_conf.gateway_ip_prefix,
					  sys_conf.owner_user, (int*)&sys_conf.ip_start, (int*)&sys_conf.ip_end);
	}
#if 0
	bzero(buf, sizeof(buf));
	ret |= sprintf(buf, "%s,%s,%s,%s,%s,%d,%d", sys_conf.download_if, 
				  sys_conf.upload_if, sys_conf.invalid_rate, sys_conf.gateway_ip_prefix,
				  sys_conf.owner_user, sys_conf.ip_start, sys_conf.ip_end);
	printf("sys info: %s\n", buf);
#endif
	if(ret <= 0) 
	{
		printf("<tc_default_conf sprintf error>: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int del_addr_node(user_info *user, char *ipaddr)
{
	addr_node *paddr = user->addr;
	addr_node *tmp = paddr;
	while(paddr)
	{
		if(!strcmp(paddr->ipaddr, ipaddr))
		{
			CLR_BIT(user->ipmask, get_ip_posfix(ipaddr) - sys_conf.ip_start);
			user->ipnum -= 1;
			iptables_do_command("-t mangle -D " DOWNLOAD_IPTABLE " -d %s -m comment --comment \"%s download rules\" -j MARK --set-mark %d", ipaddr, user->name, user->ipmark);
			iptables_do_command("-t mangle -D " UPLOAD_IPTABLE " -s %s -m comment --comment \"%s upload rules\" -j MARK --set-mark %d", ipaddr, user->name, user->ipmark);
			if(!strcmp(user->addr->ipaddr, ipaddr))
				user->addr = paddr->next;
			else
				tmp->next = paddr->next;
			free(paddr);
			return 0;
		}
		tmp = paddr;
		paddr = paddr->next;
	}
	return -1;
}

void tc_set_single_rule(user_info *ptr)
{
	user_info *tmp = NULL;
	if((tmp = if_ip_used(ptr->addr->ipaddr)) && strcmp(tmp->name, ptr->name))
	{
		printf("ip:%s is used, delete it and reset its tc rule\n", ptr->addr->ipaddr);
		del_addr_node(tmp, ptr->addr->ipaddr);	
	}

	SET_BIT(ptr->ipmask, get_ip_posfix(ptr->addr->ipaddr) - sys_conf.ip_start);
	ptr->ipnum += 1;
	iptables_do_command("-t mangle -I " DOWNLOAD_IPTABLE " 1 -d %s -m comment --comment \"%s download rules\" -j MARK --set-mark %d", ptr->addr->ipaddr, ptr->name, ptr->ipmark);
	iptables_do_command("-t mangle -I " UPLOAD_IPTABLE " 1 -s %s -m comment --comment \"%s upload rules\" -j MARK --set-mark %d", ptr->addr->ipaddr, ptr->name, ptr->ipmark);
}

void tc_set_rules()
{
	int i = 0;
	user_info *tmp, *ptr = firstuser;
	addr_node *paddr = NULL;
	char buf[16];
	tc_reset();
	tc_do_command("qdisc add dev %s root handle 1: htb default %d", sys_conf.download_if, classid);
	tc_do_command("qdisc add dev %s root handle 2: htb default %d", sys_conf.upload_if, classid);
	tc_do_command("class add dev %s parent 1: classid 1:%d htb rate %s ceil %s quantum 1500 prio 7", sys_conf.download_if, classid, sys_conf.invalid_rate, sys_conf.invalid_rate);
	tc_do_command("class add dev %s parent 2: classid 2:%d htb rate %s ceil %s quantum 1500 prio 7", sys_conf.upload_if, classid, sys_conf.invalid_rate, sys_conf.invalid_rate);
	while(ptr)
	{
		classid++;
		ptr->ipmark = use_mark;
		ptr->classid = classid;
		tc_do_command("class add dev %s parent 1: classid 1:%d htb rate %s ceil %s burst 10k prio 1", sys_conf.download_if, ptr->classid, ptr->download, ptr->download);
		tc_do_command("filter add dev %s parent 1: prio 1 handle %d fw classid 1:%d", sys_conf.download_if, ptr->ipmark, ptr->classid);
		tc_do_command("qdisc add dev %s parent 1:%d handle %d: sfq perturb 10", sys_conf.download_if, ptr->classid, ptr->ipmark);

		tc_do_command("class add dev %s parent 2: classid 2:%d htb rate %s ceil %s burst 10k prio 1", sys_conf.upload_if, ptr->classid, ptr->upload, ptr->upload);
		tc_do_command("filter add dev %s parent 2: prio 1 handle %d fw classid 2:%d", sys_conf.upload_if, ptr->ipmark, ptr->classid);
		tc_do_command("qdisc add dev %s parent 2:%d handle %d: sfq perturb 10", sys_conf.upload_if, ptr->classid, ptr->ipmark);
		
		if(!strcmp(sys_conf.owner_user, ptr->name))
		{
			printf("Creating tc root class\n");
			for(i = sys_conf.ip_start; i <= sys_conf.ip_end; i++)
			{
				if(!GET_BIT(ptr->ipmask, i - sys_conf.ip_start))
				{
					bzero(buf, sizeof(buf));
					sprintf(buf, "%s%d", sys_conf.gateway_ip_prefix, i);
					add_addr_node(ptr, buf, NULL);
				}
				SET_BIT(ptr->ipmask, i - sys_conf.ip_start);
				ptr->ipnum += 1;
				iptables_do_command("-t mangle -I " DOWNLOAD_IPTABLE  " 1 -d %s%d -m comment --comment \"%s download rules\" -j MARK --set-mark %d", sys_conf.gateway_ip_prefix, i, ptr->name, ptr->ipmark);
				iptables_do_command("-t mangle -I " UPLOAD_IPTABLE " 1 -s %s%d -m comment --comment \"%s upload rules\" -j MARK --set-mark %d", sys_conf.gateway_ip_prefix, i, ptr->name, ptr->ipmark);
			}
			ptr = ptr->next;
			use_mark++;
			continue;
		}
		
		ptr->ipnum = 0;
		bzero(ptr->ipmask, sizeof(ptr->ipmask));
		for(paddr = ptr->addr; paddr != NULL; paddr = paddr->next)
		{
			if((tmp = if_ip_used(paddr->ipaddr)) && strcmp(tmp->name, ptr->name))
			{
				printf("ip %s is used, delete it\n", paddr->ipaddr);
				del_addr_node(tmp, paddr->ipaddr);	
			}
			SET_BIT(ptr->ipmask, get_ip_posfix(paddr->ipaddr) - sys_conf.ip_start);
			ptr->ipnum += 1;
			iptables_do_command("-t mangle -I " DOWNLOAD_IPTABLE " 1 -d %s -m comment --comment \"%s download rules\" -j MARK --set-mark %d", paddr->ipaddr, ptr->name, ptr->ipmark);
			iptables_do_command("-t mangle -I " UPLOAD_IPTABLE " 1 -s %s -m comment --comment \"%s upload rules\" -j MARK --set-mark %d", paddr->ipaddr, ptr->name, ptr->ipmark);
		}

		use_mark++;
		ptr = ptr->next;
	}
}

addr_node *get_addr_node(user_info *user, const char *ipaddr)
{
	addr_node *addr = user->addr;
	while(addr)
	{
		if(!strcmp(addr->ipaddr, ipaddr))
			return addr;
		addr = addr->next;
	}
	return NULL;
}

/*
 *find a tc user
 * */
user_info* tc_get_user(const user_info* user)
{
	user_info *ptr = firstuser;
	while(ptr && ptr->name)
	{
		if(!strcmp(user->name, ptr->name))
		{
			ptr->tmp = get_addr_node(ptr, user->addr->ipaddr);
			return ptr;
		}
		ptr = ptr->next;
	}
	return NULL;
}

/*
 *insert a tc user
 * */
int tc_add_user(user_info* user)
{
	user_info *ptr = firstuser;
	user_info *tmp = firstuser;
	if(!user || !user->name || !user->addr->ipaddr)
		return -1;
	while(ptr)
	{
		if(!strcmp(user->name, ptr->name))
		{
			if(GET_BIT(ptr->ipmask, get_ip_posfix(user->addr->ipaddr) - sys_conf.ip_start))
			{
				printf("duplicate user %s: %s ignored\n", user->name, user->addr->ipaddr);
				return 0;
			}
			printf("new user name: %s ptr->name: %s\n", user->name, ptr->name);
			user->addr->next = ptr->addr;
			ptr->addr = user->addr;
			tc_set_single_rule(ptr);
			free(user);
			return 0;
		}
		tmp = ptr;
		ptr = ptr->next;
	}

	user->ipmask = ipnum%8 ? malloc((ipnum/8 + 1)*sizeof(char)) : malloc((ipnum/8)*sizeof(char));
	bzero(user->ipmask, sizeof(user->ipmask));
	SET_BIT(user->ipmask, get_ip_posfix(user->addr->ipaddr) - sys_conf.ip_start);
	if(!strcmp(sys_conf.owner_user, user->name))
	{
		printf("%s %s is the owner!\n", user->name, user->addr->ipaddr);
		//place the owner node at the first
		user->next = firstuser;
		firstuser = user;
	} else if(!firstuser){
		printf("%s %s is a new client and is the first user!\n", user->name, user->addr->ipaddr);
		firstuser = user;
	} else {
		printf("%s %s is a new client!\n", user->name, user->addr->ipaddr);
		tmp->next = user;
	}
	//reset tc rules
	tc_set_rules();
	return 0;
}

void free_user_addr(addr_node *addr)
{
	addr_node *paddr = NULL;
	while(addr)
	{
		paddr = addr;
		addr = addr->next;
		free(paddr);
	}
}

/*
 *delete a tc user
 * */
int tc_del_user_rule(user_info* ptr)
{
	addr_node *paddr = ptr->addr;
	if(!ptr)	return -1;
	//delet tc rule
	for(; paddr != NULL; paddr = paddr->next)
	{
		CLR_BIT(ptr->ipmask, get_ip_posfix(ptr->addr->ipaddr) - sys_conf.ip_start);
		ptr->ipnum -= 1;
		iptables_do_command("-t mangle -D " DOWNLOAD_IPTABLE " -d %s -m comment --comment \"%s download rules\" -j MARK --set-mark %d", ptr->addr->ipaddr, ptr->name, ptr->ipmark);
		iptables_do_command("-t mangle -D " UPLOAD_IPTABLE " -s %s -m comment --comment \"%s upload rules\" -j MARK --set-mark %d", ptr->addr->ipaddr, ptr->name, ptr->ipmark);
	}
	return 0;
}

void tc_del_user(char *user)
{
	user_info *tmp, *ptr = firstuser;
	addr_node *paddr = NULL;
	tmp = ptr;

	if(!user)
		return;
	printf("del user:%s and clear its tc rules\n", user);
	while(ptr)
	{
		if(!strcmp(user, ptr->name))
		{
			if(!strcmp(user, firstuser->name))
				firstuser = firstuser->next;
			else
				tmp->next = ptr->next;
		//	tc_del_user_rule(ptr);
			free_user_addr(ptr->addr);
			free(ptr);
			tc_set_rules();
			return;
		}
		tmp = ptr;
		ptr = ptr->next;
	}
	printf("%s not existed!\n", user);
}

/*
 * create a user_info node by cli_info
 * format of cli_info: name ip mac upload download
 * */
user_info *creat_a_user(char *cli_info)
{
	int ret = -1;
	user_info *tmp = NULL;
	if(cli_info == NULL)
		return NULL;
	tmp = malloc(sizeof(user_info));
	if(!tmp)
	{
		printf("No memeroy to malloc!\n");
		return NULL;
	}
	bzero(tmp, sizeof(tmp));
	tmp->addr = malloc(sizeof(addr_node));
	if(!tmp->addr)
	{
		printf("No memeroy to malloc!\n");
		return NULL;
	}
	bzero(tmp->addr, sizeof(addr_node));
	
	tmp->next = NULL;
	tmp->addr->next = NULL;

	ret = sscanf(cli_info, "%[^,],%[^,],%[^,],%[^,],%[^,]", tmp->name, tmp->addr->ipaddr, tmp->addr->mac, tmp->upload, tmp->download);
	printf("name:%s ipaddr:%s mac:%s upload:%s download:%s\n", tmp->name, tmp->addr->ipaddr, tmp->addr->mac, tmp->upload, tmp->download);
	if(ret != 5)
	{
		printf("cli_info is unfinished\n");
		free(tmp);
		return NULL;
	}

	return tmp;
}
