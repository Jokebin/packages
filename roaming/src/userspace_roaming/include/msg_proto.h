#ifndef __MSG_PROTO_H__
#define __MSG_PROTO_H__

#define MAC_BUF		18
#define IP4_BUF		16
#define IP6_BUF		40

#define MAC_FMT		"%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_STR(m)	m[0],m[1],m[2],m[3],m[4],m[5]

#define IP4_FMT		"%u.%u.%u.%u"
#define IP4_STR(i)	i[0],i[1],i[2],i[3]

#ifndef IFNAMSIZ
#define IFNAMSIZ	16
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

#define	ETH_DEC_LEN	6
#define IP4_DEC_LEN	4
#define IP6_DEC_LEN	16

enum {
	ST_FRESH = 1,
	ST_SERVER,
	ST_CLIENT,
	ST_MOUNTED,
};

typedef enum msg_type {
	MSG_ROAMING = 10,
	MSG_ROAMING_CLEAN,
	MSG_ROAMING_SET,
	MSG_SYNC_CLIENTS,
	MSG_SYNC_CLIENTS_REQ,
	MSG_UNKNOWN,
} t_msg;

typedef enum umsg_type {
	UMSG_HOSTAPD = 1,
	UMSG_CLI_CMD,
	UMSG_UNKNOWN,
} u_msg;

struct client_info {
	u8 cip[IP4_DEC_LEN];
	u8 cmac[ETH_DEC_LEN];
	u8 rmac[ETH_DEC_LEN];
};

typedef struct msg_roaming {
	u8 forward;
	u8 smac[ETH_DEC_LEN];
	struct client_info cinfo;
} msg_roaming_t;

typedef struct msg_roaming_resp {
	u8 client[MAC_BUF];
} msg_roaming_resp_t;

typedef struct msg_sync_clients_req {
	u8 msgid;
	u8 index;
	u8 mac[ETH_DEC_LEN];
} msg_sync_clients_req_t;

typedef struct msg_sync_clients_num {
	u8 msgid;
	u8 mac[ETH_DEC_LEN];
	u32 client_num;
} msg_sync_client_num_t;

/* first two members must the same as msg_sync_clients_num */
typedef struct msg_sync_clients_info {
	u8 msgid;
	u8 mac[ETH_DEC_LEN];
	u8 cnts;
	u8 index;
	struct client_info cinfo;
} msg_sync_client_info_t;

/********************************************/
typedef struct hostap_m {
	u8 mac[MAC_BUF];
} hostap_msg_t;

typedef struct cli_cmd {
	u8 cmd[32];
} cli_cmd_t;

typedef struct _message {
	u32 len;
	u8 type;
} message;


#define SEND_CLIENT_CNTS_ONCE	30 // 30 * 19 + 5 = 575 < 580
#define SEND_CLIENT_BUFSIZ	((SEND_CLIENT_CNTS_ONCE-1)*sizeof(struct client_info) \
		+ sizeof(message) + sizeof(msg_sync_client_info_t))

#define MSGHDR_LEN	sizeof(message)

#endif
