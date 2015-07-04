#ifndef __TRAFFIC_CONTROL_H__
#define __TRAFFIC_CONTROL_H__

#define GET_BIT(A, N) ((A[(N)/8]&(1 << (N)%8)) >> (N)%8)
#define SET_BIT(A, N) (A[(N)/8] = A[(N)/8]|(1 << (N)%8))
#define CLR_BIT(A, N) (A[(N)/8] = A[(N)/8]&(~(1 << (N)%8)))

#define MARK_START 300
#define CLASSID_START 10
#define DOWNLOAD_IPTABLE "n2lantc"
#define UPLOAD_IPTABLE "lan2ntc"
#define DEFAULT_MAC "00:11:22:33:44:55"
#define MAX_USER_NAME_LEN 32
#define DEFAULT_DOWNLOAD_IF "br-lan"
#define DEFAULT_UPLOAD_IF "br-wan"
#define DEFAULT_INVALID_RATE "5kbps"
#define DEFAULT_GATEWAY_IP_PREFIX "192.168.5."
#define DEFAULT_OWNER_USER "owner_default"
#define DEFAULT_IP_START 100
#define DEFAULT_IP_END 109

typedef struct _addr_node {
	char ipaddr[16];
	char mac[18];
	struct _addr_node *next;
} addr_node;

typedef struct _user_info {
	char name[20];
	char upload[8];
	char download[8];
	char *ipmask;
	short ipnum;
	short ipmark;
	short classid;
	addr_node *addr;
	struct _user_info* next;
	void *tmp; 
} user_info;

typedef struct _tc_sys_conf {
	char download_if[10];
	char upload_if[10];
	char invalid_rate[10];
	char gateway_ip_prefix[15];
	char owner_user[MAX_USER_NAME_LEN];
	int ip_start;
	int ip_end;
} tc_sys_conf;

void tc_clean(void);
int tc_reset(void);
/*tc_test if a ip is used*/
user_info* if_ip_used(const char *ipaddr);
int add_addr_node(user_info *user, char *ipaddr, char *mac);
/*|download_if|upload_if|invalid_rate|gateway_ip_prefix|owner_user|ip_start|ip_end|*/
int tc_set_conf(char *conf_info);
int del_addr_node(user_info *user, char *ipaddr);
void tc_set_single_rule(user_info *ptr);
void tc_set_rules();
addr_node *get_addr_node(user_info *user, const char *ipaddr);
user_info* tc_get_user(const user_info* user);
int tc_add_user(user_info* user);
void free_user_addr(addr_node *addr);
int tc_del_user_rule(user_info* ptr);
void tc_del_user(char *user);
user_info *creat_a_user(char *cli_info);

#endif
