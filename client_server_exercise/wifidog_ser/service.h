#ifndef __SERVICE_H__
#define __SERVICE_H__

#include "list.h"
#define SERVER_IP "192.168.1.208"
#define SERVER_PORT 49998
#define CLI_NAME_LEN 16
#define STORE_FILE_PATH "./data.txt"
#define QUIT "quit"
#define CAT "cat"

void delay_xx(long);
void getTime(char*);
void printfile(FILE*);

typedef struct{
	struct list_head list;
	char cli_name[CLI_NAME_LEN];
	int cli_fd;
}cli_info;

typedef struct{
//	int content_size;
	char router_ip[CLI_NAME_LEN];
	char comm[32];
	char content[128];
}mydata;

#endif
