#ifndef __READ_PCAP_H__
#define __READ_PCAP_H__

#define TMPBUFSIZ 64
/* default snap length (maximum bytes per packet to capture) */
#define SNAP_LEN 1518

/* Ethernet addresses are 6 bytes */
#define ETHER_ADDR_LEN	6

/* ethernet headers are always exactly 14 bytes */
#define SIZE_ETHERNET 14

#define SIZE_80211 34

/* Ethernet header */
struct sniff_ethernet {
	u_char ether_dhost[ETHER_ADDR_LEN]; /* Destination host address */
	u_char ether_shost[ETHER_ADDR_LEN]; /* Source host address */
	u_short ether_type; /* IP? ARP? RARP? etc */
};

/* IP header */
struct sniff_ip {
	u_char ip_vhl;		/* version << 4 | header length >> 2 */
	u_char ip_tos;		/* type of service */
	u_short ip_len;		/* total length */
	u_short ip_id;		/* identification */
	u_short ip_off;		/* fragment offset field */
#define IP_RF 0x8000		/* reserved fragment flag */
#define IP_DF 0x4000		/* dont fragment flag */
#define IP_MF 0x2000		/* more fragments flag */
#define IP_OFFMASK 0x1fff	/* mask for fragmenting bits */
	u_char ip_ttl;		/* time to live */
	u_char ip_p;		/* protocol */
	u_short ip_sum;		/* checksum */
	struct in_addr ip_src,ip_dst; /* source and dest address */
};
#define IP_HL(ip)		(((ip)->ip_vhl) & 0x0f)
#define IP_V(ip)		(((ip)->ip_vhl) >> 4)

/* TCP header */
typedef u_int tcp_seq;

struct sniff_tcp {
	u_short th_sport;	/* source port */
	u_short th_dport;	/* destination port */
	tcp_seq th_seq;		/* sequence number */
	tcp_seq th_ack;		/* acknowledgement number */
	u_char th_offx2;	/* data offset, rsvd */
#define TH_OFF(th)	(((th)->th_offx2 & 0xf0) >> 4)
	u_char th_flags;
#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PUSH 0x08
#define TH_ACK 0x10
#define TH_URG 0x20
#define TH_ECE 0x40
#define TH_CWR 0x80
#define TH_FLAGS (TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)
	u_short th_win;		/* window */
	u_short th_sum;		/* checksum */
	u_short th_urp;		/* urgent pointer */
};

/* UDP header */
struct sniff_udp {
	u_short uh_sport;
	u_short uh_dport;
	u_short uh_len;
	u_short uh_check;
};

struct pkg_node {
	struct timeval ts;
	unsigned long seq;
};

struct pkg_list {
	struct list_head list;
	struct pkg_node node;
};

struct delta_list {
	struct list_head list;
	long delta_time;
	unsigned long seq;
};

#define IP_PORT_STR_SIZE	22

struct stream_info {
	struct list_head list;

	u_char wanip_port[IP_PORT_STR_SIZE];
	u_char lanip_port[IP_PORT_STR_SIZE];
	u_char serip_port[IP_PORT_STR_SIZE];

	u_char flags;	/* 4 indicate a complete stream*/
	u_char ip_p;			/* protocol */
	u_int pkgs;				/* total pkgs */
	u_int delta_cnts;		/* delta counts*/

	unsigned long first_seq[4]; /* 0: inner_upload 1: inner_download 2: outer_upload 3: outer_download*/

	struct pkg_list streams[4]; /* 0: inner_upload 1: inner_download 2: outer_upload 3: outer_download*/
	struct delta_list delta[2]; /* 0: upload 1: download */
};

//nattable node
struct nat_node {
	u_char src_addr[IP_PORT_STR_SIZE];	
	u_char nat_addr[IP_PORT_STR_SIZE];
	u_char dst_addr[IP_PORT_STR_SIZE];
};

struct nat_table {
	struct list_head list;
	struct nat_node node;
};

enum ip_type {
	INNER_U = 0,
	OUTER_U,
	INNER_D,
	OUTER_D,
	UNKNOWN,
};

struct pcap_args {
	int id;
	int offset;
};

#define IS_OK	0x0F

//#define DEBUG

#ifdef DEBUG
#define dprintf(format, args...) printf(format, ## args)
#else
#define dprintf(format, args...) do {} while(0);
#endif

#endif
