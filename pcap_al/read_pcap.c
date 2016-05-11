#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pcap.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "list.h"
#include "read_pcap.h"

#define HOST_IP "172.58.22.140"
#define NAT_IP "192.168.1.229"
#define DEFAULT_PCAPFILE "test.pcap"
#define DEFAULT_NATFILE	"nattable"

#define IP_STR_SIZE 16

struct stream_info *streams = NULL;
struct nat_table *nattable = NULL;

u_char lanip[IP_STR_SIZE] = {0};
u_char wanip[IP_STR_SIZE] = {0};
u_char natfile[TMPBUFSIZ] = {0};
u_char wirepcap[TMPBUFSIZ] = {0};
u_char wirelesspcap[TMPBUFSIZ] = {0};

u_char output_lost_pkg = 0;

extern u_char *seq_tags[], *ts_tags[];

int fill_nattable(u_char *src_addr, u_char *addr1, u_char *addr2);
struct stream_info *new_stream();
extern int WriteStreamsToXml();

int streams_cnt = 0;

u_char *STREAM_TYPE[] = {
	"inner_upload",
	"outer_upload",
	"inner_download",
	"outer_download",
};


u_char *get_proto(u_char ip_p)
{
	u_char proto[10] = {0};
	switch(ip_p) {
		case IPPROTO_TCP:
			return "TCP";
		case IPPROTO_UDP:
			return "UDP";
		case IPPROTO_ICMP:
			return "ICMP";
		case IPPROTO_IP:
			return "IP";
		default:
			return "UNKNOW";
	}
}

//pcap_pkthdr包含三个参数：ts时间戳，caplen已捕获部分到长度，len该包到脱机长度
void pcap_callback(u_char *arg, const struct pcap_pkthdr* pkg, const u_char *content)
{
	struct pcap_args *args = (struct pcap_args *)arg;

	struct sniff_ethernet *ethd = (struct sniff_ethernet *)content; 
	struct sniff_ip *iphd = (struct sniff_ip *)(content + args->offset);
	u_char tbuf[IP_STR_SIZE] = {0};

	dprintf("id: %d\t", ++(args->id));  
	dprintf("Packet length: %d\t", pkg->len);  
	dprintf("Number of bytes: %d\t", pkg->caplen);  
	dprintf("Recieved time: %s\n", ctime((const time_t *)&pkg->ts.tv_sec));   

	dprintf("P:%s\tFrom: %s\t", get_proto(iphd->ip_p), inet_ntop(AF_INET, (void *)&iphd->ip_src, tbuf, sizeof(tbuf)));
	dprintf("\tTo:%s\n", inet_ntop(AF_INET, (void *)&iphd->ip_dst, tbuf, sizeof(tbuf)));

	struct pkg_list *list_node = (struct pkg_list *)malloc(sizeof(struct pkg_list));
	if(list_node == NULL) {
		perror("malloc");	
		return;
	}

	memset(list_node ,0, sizeof(struct pkg_list));
	memcpy(&list_node->node.ts, &pkg->ts, sizeof(struct timeval));	

	struct stream_info *tstream = streams; 
	struct sniff_tcp *tcp = NULL;
	struct sniff_udp *udp = NULL;

	//1. which proto tcp or udp
	switch(iphd->ip_p) {
		case IPPROTO_TCP:
			tcp = (struct sniff_tcp *)(content + args->offset + IP_HL(iphd)*4);
			list_node->node.seq = tcp->th_seq;
			list_node->node.seq = ntohl(tcp->th_seq);
			break;
		case IPPROTO_UDP:
			udp = (struct sniff_udp *)(content + args->offset + IP_HL(iphd)*4);
			break;
		case IPPROTO_ICMP:
		case IPPROTO_IP:
		default:
			goto ret;
	}
#if 1
	// figure out which type of package
	enum  ip_type stream_type = UNKNOWN;

	u_char src_ip[IP_STR_SIZE] = {0};
	u_char dst_ip[IP_STR_SIZE] = {0};

	if((NULL == inet_ntop(AF_INET, (void *)&iphd->ip_src, src_ip, sizeof(src_ip))) \
			|| (NULL == inet_ntop(AF_INET, (void *)&iphd->ip_dst, dst_ip, sizeof(dst_ip)))) {
		perror("inet_ntop");
		goto ret;
	}

	if(!memcmp(wanip, src_ip, IP_STR_SIZE)) {
		stream_type = OUTER_U;
	} else if(!memcmp(wanip, dst_ip, IP_STR_SIZE)) {
		stream_type = OUTER_D;
	} else if(!memcmp(lanip, src_ip, IP_STR_SIZE)) {
		stream_type = INNER_U;
	} else if(!memcmp(lanip, dst_ip, IP_STR_SIZE)) {
		stream_type = INNER_D;
	} else
		goto ret;

	u_char dst_key[IP_PORT_STR_SIZE] = {0};
	u_char src_key[IP_PORT_STR_SIZE] = {0};
	sprintf(src_key, "%s:%d", inet_ntoa(iphd->ip_src), (iphd->ip_p == IPPROTO_TCP) ? ntohs(tcp->th_sport) : ntohs(udp->uh_sport));
	sprintf(dst_key, "%s:%d", inet_ntoa(iphd->ip_dst), (iphd->ip_p == IPPROTO_TCP) ? ntohs(tcp->th_dport) : ntohs(udp->uh_dport));

	//printf("%s %s %ld\n", src_ip, dst_ip, list_node->seq);

	//2. found stream
	struct stream_info *pos, *next;
	list_for_each_entry_safe(pos, next, &tstream->list, list) {

		if((!memcmp(src_key, (stream_type == OUTER_U || stream_type == OUTER_D) ? pos->wanip_port : pos->lanip_port, IP_PORT_STR_SIZE) \
					&& !memcmp(dst_key, pos->serip_port, IP_PORT_STR_SIZE)) \
				|| (!memcmp(dst_key, (stream_type == OUTER_U || stream_type == OUTER_D) ? pos->wanip_port : pos->lanip_port, IP_PORT_STR_SIZE) \
					&& !memcmp(src_key, pos->serip_port, IP_PORT_STR_SIZE))) {

			dprintf("Found a %s stream seq %ld\n", STREAM_TYPE[stream_type], list_node->node.seq);

			if(!(pos->flags & (1 << stream_type))) {
				pos->flags |= (1 << stream_type);
			}

			if((IPPROTO_TCP == pos->ip_p) && (0 == pos->first_seq[stream_type])) {
				pos->first_seq[stream_type] = list_node->node.seq;
			}

			// pkg inserted with increasing seq order 
			struct pkg_list *p_pkg, *p_next;
			list_for_each_entry_tail_safe(p_pkg, p_next, &pos->streams[stream_type].list, list) {
				if(list_node->node.seq >= p_pkg->node.seq) {
					list_add(&list_node->list, &p_pkg->list);
					break;
				}
			}

			if(&p_pkg->list == &p_next->list)
				list_add_tail(&list_node->list, &pos->streams[stream_type].list);
			pos->pkgs++;
			return;
		}
	}

	if(stream_type == INNER_U || stream_type == INNER_D)
		goto ret;

	if(&pos->list == &tstream->list) {
		struct stream_info *nstream = new_stream();
		u_char *port_index = NULL;
		u_char lan_key[] = {0};

		printf("Find a new stream %s-->%s\n", src_key, dst_key);
		//lookup nat table lanip:port=>wanip:port --->serip:port
		//and fillup the inner lanip_port

		nstream->flags |= (1 << stream_type);
		nstream->first_seq[stream_type] = list_node->node.seq;

		memcpy(nstream->wanip_port, (stream_type == OUTER_U) ? src_key : dst_key, IP_PORT_STR_SIZE);
		memcpy(nstream->serip_port, (stream_type == OUTER_U) ? dst_key : src_key, IP_PORT_STR_SIZE);

		port_index = strchr(nstream->wanip_port, ':');
		sprintf(nstream->lanip_port, "%s%s", lanip, port_index);

		list_add_tail(&list_node->list, &nstream->streams[stream_type].list);
		list_add(&nstream->list, &streams->list);
		nstream->ip_p = iphd->ip_p;	
		nstream->pkgs++;
		streams_cnt++;

		return;
	}

#else
	int i;
	for(i = 0; i < pkg->len; i++) {
		printf(" %02x", content[i]);	
		if((i + 1)%16 == 0) {
			dprintf("\n");
		}
	}
	dprintf("\n");
	return;
#endif
ret:
	free(list_node);

}

struct stream_info *new_stream()
{
	struct stream_info *new_stream = (struct stream_info *)malloc(sizeof(struct stream_info));
	if(!new_stream) {
		perror("malloc");
		return NULL;
	}
	memset(new_stream, 0, sizeof(struct stream_info));

	int i = 0;

	for(; i < UNKNOWN; i++)
		INIT_LIST_HEAD(&new_stream->streams[i].list);

	INIT_LIST_HEAD(&new_stream->delta[0].list);
	INIT_LIST_HEAD(&new_stream->delta[1].list);

	return new_stream;
}

// open and iterate pcap file
int iterate_pcap(char *filename, pcap_handler callback, char *filters, void *args)
{
	pcap_t *handle;//会话句柄
	char error[100];//存储错误信息字符串
	struct bpf_program filter;//已经编译好的过滤器

	if((handle = pcap_open_offline(filename, error)) == NULL)  //打开文件
	{
		printf("%s\n", error);
		return 0;
	}

	pcap_compile(handle, &filter, filters, 1, 0);//函数返回-1为失败

	if(pcap_setfilter(handle, &filter))//成功返回0.不成功返回-1
		return -1;

	int ret = pcap_loop(handle, -1, callback, (u_char*)args);  //捕获数据包

	/* cleanup */
	pcap_freecode(&filter);
	pcap_close(handle);

	return 0;
}

// lookup nattable
int fill_nattable(u_char *src_addr, u_char *addr1, u_char *addr2)
{
	struct nat_table *pos, *next;
	list_for_each_entry_safe(pos, next, &nattable->list, list) {
		if(!memcmp(pos->node.nat_addr, addr1, IP_PORT_STR_SIZE) \
				&& !memcmp(pos->node.dst_addr, addr2, IP_PORT_STR_SIZE)) {

			dprintf("src_addr: %s\n", pos->node.src_addr);
			memcpy(src_addr, pos->node.src_addr, IP_PORT_STR_SIZE);

			return 1;
		}
	}

	return 0;
}

// init nattable from nattable file
int parse_nattable(char *natfile)
{
	if(NULL == natfile || NULL == nattable) {
		printf("[ERROR] no natfile provided\n");
		return -1;
	}

	FILE *p = fopen(natfile, "r");
	u_char rbuf[128] = {0};

	if(NULL == p) {
		perror("fopen");
		return -1;
	}

	printf("Nat table:\n");
	printf("---------------------------------------------------------------------------------------\n");
	while(NULL != fgets(rbuf, 128, p)) {
		struct nat_table *tmp = (struct nat_table *) malloc(sizeof(struct nat_table));
		sscanf(rbuf, "%s %s %s", tmp->node.src_addr, tmp->node.nat_addr, tmp->node.dst_addr);	
		printf("%s --> %s --> %s\n", tmp->node.src_addr, tmp->node.nat_addr, tmp->node.dst_addr);
		list_add_tail(&tmp->list, &nattable->list);
	}

	printf("---------------------------------------------------------------------------------------\n");
	fclose(p);

	return 0;
}

void delete_stream(struct stream_info *stream)
{
	if(NULL == stream)
		return;

	struct pkg_list *pos, *pnext;
	u_char stream_type = 0;

	// first, empty the stream
	for(; stream_type < UNKNOWN; stream_type++) {
		list_for_each_entry_safe(pos, pnext, &stream->streams[stream_type].list, list) {
			list_del(&pos->list);
			free(pos);
		}	
	}

	// then, del and free the stream
	list_del(&stream->list);
	free(stream);
}

void __adjust_streams(struct stream_info *pos, u_char first, u_char second)
{

	if((NULL == pos) || (pos->first_seq[first] == pos->first_seq[second]))
		return;

	struct pkg_list *plist, *pnext;
	u_char tochange = 0, unchange = 0;

	if(pos->first_seq[first] < pos->first_seq[second]) {
		tochange = first;
		unchange = second;
	} else {
		tochange = second;
		unchange = first;
	}

	list_for_each_entry_safe(plist, pnext, &pos->streams[tochange].list, list) {
		if(plist->node.seq != pos->first_seq[unchange]) {
			list_del(&plist->list);
			free(plist);
			pos->pkgs--;
		}
	}
	pos->first_seq[tochange] = pos->first_seq[unchange];
}

// adjust tcp streams, let the start seq of inner_upload and inner_download to match
// with outer_upload and outer_download
// not handle the udp streams
// delete invalid streams
void adjust_streams(struct stream_info *tstream)
{
	struct stream_info *pos, *next;

	list_for_each_entry_safe(pos, next, &tstream->list, list) {

		// streams only inner_lan or outer_lan have pkgs is invalid
		if((pos->flags & IS_OK) != IS_OK) {
			delete_stream(pos);
			streams_cnt--;
		}

		if(pos->ip_p != IPPROTO_TCP)
			continue;

		// adjust download streams
		__adjust_streams(pos, INNER_U, OUTER_U);

		// adjust download streams
		__adjust_streams(pos, INNER_D, OUTER_D);
	}
}

void __caculte_delta(u_char orig, u_char result, struct delta_list *delta, struct stream_info *st)
{
	if(!st || !delta)
		return;

	struct pkg_list *p_orig = list_entry(st->streams[orig].list.next, struct pkg_list, list);
	struct pkg_list *p_results = list_entry(st->streams[result].list.next, struct pkg_list, list);
	struct delta_list *p_delta = delta;

	do {

		if(p_orig->node.seq > p_results->node.seq) {
			//results pcap start earlier than orig pcap
			struct pkg_list *pos = p_results, *next;
			do {
				pos = list_entry(pos->list.next, struct pkg_list, list);	
				if(pos->node.seq >= p_orig->node.seq)
					break;
			} while (pos != &st->streams[result]);

			if(pos == &st->streams[result]) {
				printf("Invalid stream\n");
				break;
			}

			p_results = pos;
		} else {

			if(!output_lost_pkg && (p_orig->node.seq < p_results->node.seq)) {
				st->lost_cnts++;
				p_orig = list_entry(p_orig->list.next, struct pkg_list, list);
				continue;
			}

			struct delta_list *ptmp = (struct delta_list *)malloc(sizeof(struct delta_list));
			if(NULL == ptmp) {
				perror("malloc");
				continue;
			}

			ptmp->seq = p_orig->node.seq - st->first_seq[orig];

			if(p_orig->node.seq == p_results->node.seq) {
				ptmp->delta_time = (p_results->node.ts.tv_sec - p_orig->node.ts.tv_sec) * 1000000;
				ptmp->delta_time = ptmp->delta_time + p_results->node.ts.tv_usec - p_orig->node.ts.tv_usec;
				p_results = list_entry(p_results->list.next, struct pkg_list, list);
			} else {
				st->lost_cnts++;
				ptmp->delta_time = -100 * 1000;
			}

			list_add(&ptmp->list, &p_delta->list);
			p_delta = list_entry(p_delta->list.next, struct delta_list, list);
			p_orig = list_entry(p_orig->list.next, struct pkg_list, list);
			st->delta_cnts++;
		}

	} while((NULL != p_orig) && (p_orig != &st->streams[orig]));
}

// caculte delta time of upload and download streams
void caculte_delta()
{
	if(NULL == streams) {
		printf("%s: stream is null\n", __func__);
		return;
	}

	struct stream_info *pos, *next;
	list_for_each_entry_safe(pos, next, &streams->list, list) {

		if((pos->flags & IS_OK) != IS_OK)
			continue;

		__caculte_delta(INNER_U, OUTER_U, &pos->delta[0], pos);
		__caculte_delta(OUTER_D, INNER_D, &pos->delta[1], pos);
	}
}

void print_streams()
{
	if(NULL == streams) return;
	printf("---------------------------------------------------------------------------------------\n");

	struct stream_info *pos, *next;
	u_char stream_type = 0;

	list_for_each_entry_safe(pos, next, &streams->list, list) { 

		printf("From %s Via %s to %s Total %d Proto %s\n", pos->lanip_port, pos->wanip_port, \
				pos->serip_port, pos->pkgs, (pos->ip_p == IPPROTO_TCP) ? "TCP" : "UDP");	

		if(pos->ip_p != IPPROTO_TCP)
			continue;
		for(stream_type = 0; stream_type < UNKNOWN; stream_type++) {
			printf("\t%s seq_start: %ld\n", ts_tags[stream_type], pos->first_seq[stream_type]);
		}
	}
	printf("Total %d streams\n", streams_cnt);
	printf("---------------------------------------------------------------------------------------\n");
}

void default_config()
{
	snprintf(wirepcap, TMPBUFSIZ, "%s", DEFAULT_PCAPFILE);
	//snprintf(wirelesspcap, TMPBUFSIZ, "%s", DEFAULT_PCAPFILE);
	snprintf(natfile, TMPBUFSIZ, "%s", DEFAULT_NATFILE);
	snprintf(lanip, IP_STR_SIZE, "%s", HOST_IP);
	snprintf(wanip, IP_STR_SIZE, "%s", NAT_IP);
	memset(wirelesspcap, 0, TMPBUFSIZ);
}

void print_config()
{
	printf("--Default config-----------------------------------------------------------------------\n");
	printf("\tPcapWire: %s\n", wirepcap);
	printf("\tPcapWireless: %s\n", wirepcap);
	printf("\tNatfile: %s\n", natfile);
	printf("\tLanip: %s\n", lanip);
	printf("\tWanip: %s\n", wanip);
	printf("---------------------------------------------------------------------------------------\n");
}

void usage(char *cmd)
{
	printf("Usage: %s [-s] [-l ip] [-g ip] [-f wirepcap] [-n file] [-w wirelesspcap]\n", cmd);
	printf("\t -l: ip of lan host, default %s\n", HOST_IP);
	printf("\t -g: ip of gw-interface, default %s\n", NAT_IP);
	printf("\t -f: set the wire pcap file, default %s\n", DEFAULT_PCAPFILE);
	printf("\t -w: set the wireless pcap file, default null\n");
	printf("\t -n: set the nat file, default %s\n", DEFAULT_NATFILE);
	printf("\t -s: whether to output lost_pkg, default no\n");
	printf("\t -h: print this msg\n");
}

int main(int argc, char *argv[])
{
	int ret = 0;

	//default config
	default_config();

	// get options from commandline
	while((ret = getopt(argc, argv, ":l:f:g:w:n:s:h")) != -1) {
		switch(ret) {
			case 'l':
				snprintf(lanip, IP_STR_SIZE, "%s", optarg);
				break;
			case 'f':	/* read the file, include the username and passwd*/
				snprintf(wirepcap, TMPBUFSIZ, "%s", optarg);
				break;
			case 'g':
				snprintf(wanip, IP_STR_SIZE, "%s", optarg);
				break;
			case 'w':
				snprintf(wirelesspcap, TMPBUFSIZ, "%s", optarg);
				break;
			case 'n':
				snprintf(natfile, TMPBUFSIZ, "%s", optarg);
				break;
			case 's':
				output_lost_pkg = atoi(optarg);
				break;
			case 'h':
			default:
				usage(argv[0]);
				return -1;
		}
	}

	print_config();

	streams = new_stream();
	INIT_LIST_HEAD(&streams->list);

#if 0
	nattable = (struct nat_table*)malloc(sizeof(struct nat_table));
	if(NULL == nattable) {
		perror("malloc");
		return -1;
	}

	memset(nattable, 0, sizeof(struct nat_table));
	INIT_LIST_HEAD(&nattable->list);
	if(parse_nattable(natfile))
		goto free;
#endif

	u_char filter_str[BUFSIZ] = {0};
	struct pcap_args args = {0, SIZE_ETHERNET};
	// 1 iterator: src<wanip:port>--->dst<serip:port> to decline a stream
	//snprintf(filter_str, BUFSIZ, "host %s ", lanip);
	snprintf(filter_str, BUFSIZ, "tcp and host %s and not src and dst net %s", wanip, lanip);
	iterate_pcap(wirepcap, pcap_callback, filter_str, &args);

	// 2 iterator: src<lanip:port>--->dst<serip:port> and nat to decline a stream
	snprintf(filter_str, BUFSIZ, "tcp and host %s and not src and dst net %s", lanip, wanip);
	iterate_pcap(wirepcap, pcap_callback, filter_str, &args);
	if(0 != strlen(wirelesspcap)) {
		args.id = 0;
		args.offset = SIZE_80211;
		iterate_pcap(wirelesspcap, pcap_callback, filter_str, &args);
	}

	printf("---------------------------------------------------------------------------------------\n");
	printf("---------------------------------------------------------------------------------------\n");
	print_streams();
	printf("---------------------------------------------------------------------------------------\n");
	printf("---------------------------------------------------------------------------------------\n");

	//adjust_streams(streams);
	caculte_delta();
	WriteStreamsToXml();
	printf("---------------------------------------------------------------------------------------\n");
	printf("---------------------------------------------------------------------------------------\n");

free:
	free(streams);
	free(nattable);

	return 0;
} 
