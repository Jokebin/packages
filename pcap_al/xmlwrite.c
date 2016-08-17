#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "list.h"
#include "read_pcap.h"

extern struct stream_info *streams;

int WriteStreamsToXml();

u_char *seq_tags[] = {
	"Seq_Iupload",
	"Seq_Oupload",
	"Seq_Idownload",
	"Seq_Odownload",
	"Upload",
	"Download",
};

u_char *ts_tags[] = {
	"Iupload_ms",
	"Oupload_ms",
	"Idownload_ms",
	"Odownload_ms",
	"Up_us",
	"Down_us",
};

void test()
{

}

/*
 *  root
 *   |
 *  node1--->AddrStr-->Seq1-->Inner_upload-->Seq2-->Outer_upload-->Seq3-->Inner_download-->Seq4-->Outer_download
 *   |
 *  node2--->AddrStr-->Seq1-->Inner_upload-->Seq2-->Outer_upload-->Seq3-->Inner_download-->Seq4-->Outer_download
 *   |
 *  node3--->AddrStr-->Seq1-->Inner_upload-->Seq2-->Outer_upload-->Seq3-->Inner_download-->Seq4-->Outer_download
 *   |
 *  node4--->AddrStr-->Seq1-->Inner_upload-->Seq2-->Outer_upload-->Seq3-->Inner_download-->Seq4-->Outer_download
 *   .
 *
*/

u_char* XmlDocName(u_char *docname, u_char *lanip_port, u_char *serip_port, u_char proto)
{
	if(!docname || !lanip_port || !serip_port ||(proto != IPPROTO_TCP && proto != IPPROTO_UDP)) {
		printf("invalid filename");
		return NULL;
	}

	u_char tdocname[TMPBUFSIZ] = {0};
	u_char *index = tdocname;

	memset(docname, 0, TMPBUFSIZ);
	snprintf(tdocname, TMPBUFSIZ - 1, "%s_%s_%s", (proto == IPPROTO_TCP) ? "tcp" : "udp", lanip_port, serip_port);

	while(*index != '\0') {
		if(('.' == *index) || (':' == *index))
			*index = '_';
		index++;
	}

	snprintf(docname, TMPBUFSIZ, "%s/%s.xml", "./data", tdocname);
	printf("Docname: %s\n", docname);

	return docname;
}

int WriteStreamsToXml()
{
	if(NULL == streams) {
		printf("No valid streams captured!\n");
		return -1;
	}

	u_int pkgs = 0, cnts;
	u_char xmlname[TMPBUFSIZ] = {0};
	u_char addrstr[TMPBUFSIZ] = {0};
	u_char ids[TMPBUFSIZ] = {0};
	u_char seq_str[TMPBUFSIZ] = {0};
	u_char ts_str[TMPBUFSIZ] = {0};

	xmlNodePtr root_node = NULL, new_node = NULL;
	xmlDocPtr new_doc = NULL;

	struct pkg_list *pstream[4];
	struct stream_info *pos, *next;
	u_char stream_type = 0;

	struct delta_list *p_delta[2];
	int i = 0;

	list_for_each_entry_safe(pos, next, &streams->list, list) {

		// 1. create a xml doc for this stream
		if(!XmlDocName(xmlname, pos->lanip_port, pos->serip_port, pos->ip_p))
			continue;

		new_doc = xmlNewDoc(BAD_CAST "1.0"); 
		root_node = xmlNewNode(NULL, BAD_CAST "streams");
		xmlDocSetRootElement(new_doc, root_node);

		pkgs = pos->delta_cnts;
		cnts = 0;
#ifdef SMALL
		pkgs = pos->pkgs;
		for(stream_type = 0; stream_type < UNKNOWN; stream_type++)
			pstream[stream_type] = list_entry(pos->streams[stream_type].list.next, struct pkg_list, list);
#endif
		for(i = 0; i < 2; i++)
			p_delta[i] = list_entry(pos->delta[i].list.next, struct delta_list, list);

		snprintf(addrstr, TMPBUFSIZ, "%s_%s", pos->lanip_port, pos->serip_port);

		// 2. filling all packages in the xml-doc with above format 
		while(pkgs > 0) {

			if(cnts != 0)
				addrstr[0] = '\0';
			snprintf(ids, sizeof(ids), "%d", cnts++);
			new_node = xmlNewChild(root_node, NULL, BAD_CAST "stream", NULL);
			xmlNewProp(new_node, BAD_CAST "Addr", BAD_CAST addrstr);
			xmlNewChild(new_node, NULL, BAD_CAST "id", BAD_CAST ids);
#ifdef SMALL
			for(stream_type = 0; stream_type < UNKNOWN; stream_type++) {
				if(&pstream[stream_type]->list != &pos->streams[stream_type].list) {
					snprintf(seq_str, TMPBUFSIZ, "%ld", pstream[stream_type]->node.seq);
					snprintf(ts_str, TMPBUFSIZ, "%ld", ((pstream[stream_type]->node.ts.tv_sec%10000)*1000000 + pstream[stream_type]->node.ts.tv_usec)); //time scale is ms
					xmlNewChild(new_node, NULL, BAD_CAST seq_tags[stream_type], BAD_CAST seq_str);
					xmlNewChild(new_node, NULL, BAD_CAST ts_tags[stream_type], BAD_CAST ts_str);
					pstream[stream_type] = list_entry(pstream[stream_type]->list.next, struct pkg_list, list);
					pkgs--;
				}
			}
#endif
			for(i = 0; i < 2; i++) {
				if(p_delta[i] != &pos->delta[i]) {
					snprintf(seq_str, TMPBUFSIZ, "%ld", p_delta[i]->seq);
					snprintf(ts_str, TMPBUFSIZ, "%ld", p_delta[i]->delta_time/1000); //time scale is us
					xmlNewChild(new_node, NULL, BAD_CAST seq_tags[i + UNKNOWN], BAD_CAST seq_str);
					xmlNewChild(new_node, NULL, BAD_CAST ts_tags[i + UNKNOWN], BAD_CAST ts_str);
					p_delta[i] = list_entry(p_delta[i]->list.next, struct delta_list, list);
#ifndef SMALL
					pkgs--;
#endif
				}
			}
		}
		// 3. store the xml-doc with name "lanip_port_serip_port.xml"
		xmlSaveFormatFileEnc(xmlname, new_doc, "UTF-8", 1);
		xmlFreeDoc(new_doc);
		xmlCleanupParser();
		xmlMemoryDump();
	}

	return 0;
}

#if 0
int main(int argc, char **argv)
{
	xmlDocPtr doc = NULL;
	xmlNodePtr root_node = NULL, node = NULL, node1 = NULL;

	doc = xmlNewDoc(BAD_CAST "1.0");
	root_node = xmlNewNode(NULL, BAD_CAST "root");

	xmlDocSetRootElement(doc, root_node);

	node = xmlNewChild(root_node, NULL, BAD_CAST "stream", NULL);
	xmlNewProp(node, BAD_CAST "id", BAD_CAST "1");

	xmlNewChild(node, NULL, BAD_CAST "From", BAD_CAST "172.58.22.140");
	xmlNewChild(node, NULL, BAD_CAST "To", BAD_CAST "192.168.1.104");
	xmlNewChild(node, NULL, BAD_CAST "Seq1", BAD_CAST "12360");
	xmlNewChild(node, NULL, BAD_CAST "Inner_upload", BAD_CAST "555550");
	xmlNewChild(node, NULL, BAD_CAST "Seq2", BAD_CAST "12360");
	xmlNewChild(node, NULL, BAD_CAST "Outer_upload", BAD_CAST "555550");

	xmlNewChild(node, NULL, BAD_CAST "Seq3", BAD_CAST "12360");
	xmlNewChild(node, NULL, BAD_CAST "Inner_download", BAD_CAST "555550");
	xmlNewChild(node, NULL, BAD_CAST "Seq4", BAD_CAST "12360");
	xmlNewChild(node, NULL, BAD_CAST "Outer_download", BAD_CAST "555550");

	node1 = xmlNewChild(root_node, NULL, BAD_CAST "stream", NULL);
	xmlNewProp(node1, BAD_CAST "id", BAD_CAST "2");

	xmlNewChild(node1, NULL, BAD_CAST "From", BAD_CAST "172.58.22.140");
	xmlNewChild(node1, NULL, BAD_CAST "To", BAD_CAST "192.168.1.104");
	xmlNewChild(node1, NULL, BAD_CAST "Seq1", BAD_CAST "12360");
	xmlNewChild(node1, NULL, BAD_CAST "Inner_upload", BAD_CAST "555550");
	xmlNewChild(node1, NULL, BAD_CAST "Seq2", BAD_CAST "12360");
	xmlNewChild(node1, NULL, BAD_CAST "Outer_upload", BAD_CAST "555550");

	xmlNewChild(node1, NULL, BAD_CAST "Seq3", BAD_CAST "12360");
	xmlNewChild(node1, NULL, BAD_CAST "Inner_download", BAD_CAST "555550");
	xmlNewChild(node1, NULL, BAD_CAST "Seq4", BAD_CAST "12360");
	xmlNewChild(node1, NULL, BAD_CAST "Outer_download", BAD_CAST "555550");
#if 0
	node = xmlNewNode(NULL, BAD_CAST "node3");
	node1 = xmlNewText(BAD_CAST "other way to create content");

	xmlAddChild(node, node1);
	xmlAddChild(root_node, node);
#endif	

	xmlSaveFormatFileEnc(argc > 1 ? argv[1] : "xxx.xml", doc, "UTF-8", 1);
	
	xmlFreeDoc(doc);
	xmlCleanupParser();
	xmlMemoryDump();


	return 0;
}

#endif
