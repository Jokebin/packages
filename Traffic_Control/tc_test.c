#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>

#include "traffic_control.h"
#include "util.h"

extern user_info *firstuser;
extern tc_sys_conf sys_conf;
char buf[128];

/*
 *tc_test
 * */
void tc_test()
{
	char buf[128];
#if 1
	bzero(buf, sizeof(buf));
	sprintf(buf, "%s %s %s %s %s", "zyc", "192.168.5.103", "00:11:22:33:44:56", "300kbps", "300kbps");
	tc_add_user(creat_a_user(buf));

	bzero(buf, sizeof(buf));
	sprintf(buf, "%s %s %s %s %s", "zyc", "192.168.5.104", "00:11:22:33:44:56", "300kbps", "300kbps");
	tc_add_user(creat_a_user(buf));

	bzero(buf, sizeof(buf));
	sprintf(buf, "%s %s %s %s %s", "zyc", "192.168.5.105", "00:11:22:33:44:56", "300kbps", "300kbps");
	tc_add_user(creat_a_user(buf));

	bzero(buf, sizeof(buf));
	sprintf(buf, "%s %s %s %s %s", "wokao", "192.168.5.101", "00:11:22:33:44:55", "150kbps", "150kbps");
	tc_add_user(creat_a_user(buf));

	bzero(buf, sizeof(buf));
	sprintf(buf, "%s %s %s %s %s", "wokao", "192.168.5.102", "00:11:22:33:44:55", "150kbps", "150kbps");
	tc_add_user(creat_a_user(buf));

	bzero(buf, sizeof(buf));
	sprintf(buf, "%s %s %s %s %s", firstuser->name, "192.168.5.101", firstuser->addr->mac, firstuser->upload, firstuser->download);
	tc_add_user(creat_a_user(buf));
#endif
	tc_del_user("wokao");
}

void get_input(char *buf)
{
	int c = -1;
	bzero(buf, sizeof(buf));
	printf("Input: ");
	fflush(stdout);
	scanf("%s", buf);
	while((c = getchar()) != '\n' && c != EOF);
	if(strlen(buf) <= 0)
	{
		printf("your input is invalid, try again\n");
		return;
	}
}

void test_add_use()
{
	printf("Pls input new user info\n");
	printf("Info format: name,ipaddr,mac,upload,download\n");
	get_input(buf);
	tc_add_user(creat_a_user(buf));
}

void test_del_use()
{
	printf("Pls input the username want to del\n");
	get_input(buf);
	tc_del_user(buf);	
}

void test_set_conf()
{
	printf("Pls set the sys config\n");
	printf("Config format: download_if,upload_if,invalid_rate,gateway_ip_prefix,owner_user,ip_start,ip_end\n");
	get_input(buf);
	tc_set_conf(buf);
}

void print_use()
{
	system("/usr/sbin/iptables -n --line-numbers -t mangle -L POSTROUTING -v");
	bzero(buf, sizeof(buf));
	sprintf(buf, "/usr/sbin/iptables -n --line-numbers -t mangle -L %s -v", DOWNLOAD_IPTABLE);
	system(buf);
	bzero(buf, sizeof(buf));
	sprintf(buf, "/usr/sbin/iptables -n --line-numbers -t mangle -L %s -v", UPLOAD_IPTABLE);
	system(buf);
}

void usage()
{
	system("clear");
	printf("**************************************************\n");
	printf("*Pls input command <set|add|del|clean|print|quit>*\n");
	printf("* set: set sys conf                              *\n");
	printf("* add: add a user                                *\n");
	printf("* del: del a user                                *\n");
	printf("* clean: clean all user                          *\n");
	printf("* print: print user info                         *\n");
	printf("* quit: exit test system                         *\n");
	printf("**************************************************\n");
}

int main(int argc, char **argv)
{
	fd_set rdset;
	bzero(buf, sizeof(buf));
	tc_set_conf(NULL);
	while(1)
	{
		FD_ZERO(&rdset);
		FD_SET(0, &rdset);
		if(select(1, &rdset, NULL, NULL, NULL) < 0)
			continue;
		if(FD_ISSET(0, &rdset))
		{
			usage();
			get_input(buf);
			if(!strcmp("set", buf))
			{
				test_set_conf();
				continue;
			}
			if(!strcmp("add", buf))	
			{
				test_add_use();
				continue;
			}

			if(!strcmp("del", buf))
			{
				test_del_use();
				continue;
			}

			if(!strcmp("clean", buf))
			{
				tc_clean();
				continue;
			}
			
			if(!strcmp("print", buf))
			{
				print_use();
				continue;
			}

			if(!strcmp("quit", buf))
			{
				tc_clean();
				break;
			}
		}
	}

	return 0;
}

