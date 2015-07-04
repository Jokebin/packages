#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/time.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include "list.h"
#include "service.h"
#include "send_thread.h"

cli_info client_info;

int main(void)
{
	int ret = -1;
	int listen_fd = -1,new_fd = -1;
	FILE *st_fd = NULL;//保存客户端发送内容文件

	fd_set rdset;
	int maxfd = -1;
	mydata rcv_buf;
	cli_info *tmp;
	struct list_head *pos,*q;

	struct sockaddr_in sin,cli_addr;
	int cli_size = sizeof(cli_addr);
	char buf[BUFSIZ];
	char mark_buf[256];
	char tbuf[20];
	char *flag_buf = NULL;
	//open file and create it if not existed
	st_fd = fopen(STORE_FILE_PATH, "ab+");
	if(st_fd == NULL)
	{
		perror("open st_fd failed");
		goto retur;
	}

	//init_list_head
	INIT_LIST_HEAD(&client_info.list);

	//socket
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(listen_fd < 0)
	{
		perror("create socket failed");
		goto retur;
	}

	//set socket option addr reused 
	int re_use = 1;
	if(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &re_use, sizeof(int)) != 0)
	{
		perror("setsockopt failed");
		goto fail;
	}

	//init sockaddr_in and bind socket
	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(SERVER_PORT);
//	sin.sin_addr.s_addr = inet_addr(SERVER_IP);
	if(inet_pton(AF_INET, SERVER_IP, &sin.sin_addr) <= 0)
	{
		perror("inet_pton failed");
		goto fail;
	}
	ret = bind(listen_fd, (struct sockaddr *)&sin, sizeof(sin));
	if(ret != 0)
	{
		perror("bind failed");
		goto fail;
	}

	//listen socket
	listen(listen_fd, 5);

	//accept
	while(1)
	{
		FD_ZERO(&rdset);
		FD_SET(0, &rdset);
		FD_SET(listen_fd, &rdset);
		maxfd = listen_fd;
		list_for_each_safe(pos, q, &client_info.list)
		{
			tmp = list_entry(pos, cli_info, list);
			FD_SET(tmp->cli_fd, &rdset);
			if(tmp->cli_fd > maxfd)
			{
				maxfd = tmp->cli_fd;
			}
		}

		if(select(maxfd + 1, &rdset, NULL, NULL, NULL) < 0)
			continue;
		if(FD_ISSET(listen_fd, &rdset))
		{
			//client_connect
			//printf("client_connect\n");
			memset(&cli_addr, 0, sizeof(cli_addr));
			new_fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &cli_size);
			tmp =(cli_info *)malloc(sizeof(cli_info));
			assert(tmp != NULL);
			tmp->cli_fd = new_fd;
			if(inet_ntop(AF_INET, (void*)&cli_addr.sin_addr, tmp->cli_name, cli_size) < 0)
				continue;
			tmp->cli_name[CLI_NAME_LEN - 1] = '\0';
			list_add(&(tmp->list), &(client_info.list));
		}

		if(FD_ISSET(0, &rdset))
		{
			//admin input
			system("clear");
			printf("pls input!\n");
			scanf("%s", buf);
			printf("input_____:%s\n",buf);
			if(!strcmp(buf, QUIT))	break;//cmd quit
			if(!strcmp(buf, CAT))		//cmd cat
			{
				printfile(st_fd);	
			}
		}
		
		list_for_each_safe(pos, q, &client_info.list)
		{
			tmp = list_entry(pos, cli_info, list);
			if(FD_ISSET(tmp->cli_fd, &rdset))
			{
				//cli_fd connected
				memset(buf, 0, sizeof(buf));
				memset(mark_buf, 0, sizeof(mark_buf));
				do{
					ret = read(tmp->cli_fd, buf, sizeof(buf));
				}while(ret == EAGAIN || ret == EINTR);
				if(ret == EBADF)	continue;
				if(ret > 0)
				{
				//	system("clear");
					getTime(tbuf);
					printf("%s%s: %s\n", tbuf, tmp->cli_name, buf);
					fprintf(st_fd, "%s%s: %s\n", tbuf, tmp->cli_name, buf);//数据写入文件保存
					fflush(st_fd);
					memset(&rcv_buf, 0, sizeof(rcv_buf));
					//客户端发来的数据格式为：key#client_ip@client_mac#
					sscanf(tmp->cli_name, "%[^\\s]", rcv_buf.router_ip);
					sscanf(buf,"%[^#]%s", mark_buf, rcv_buf.content);
				//	printf("mark_buf = %s_extra = %s\n", mark_buf, rcv_buf.content);
				}
				if(!strcmp(AUTH_OK_MSG, mark_buf))
				{
					flag_buf = strdup(AUTH_OK);
					ret = write(tmp->cli_fd, flag_buf, strlen(flag_buf));
					sscanf(flag_buf, "%s", rcv_buf.comm);
					printf("router_ip = %s\n", rcv_buf.router_ip);
					printf("comm = %s\ncontent = %s\n", rcv_buf.comm, rcv_buf.content);
				//	send_thread_func(&rcv_buf);
				}else{
					printf("auth failed!\n");
					flag_buf = strdup(AUTH_FAILED);
					ret = write(tmp->cli_fd, flag_buf, sizeof(flag_buf));
				}
				close(tmp->cli_fd);
				list_del(&tmp->list);
			}
		}
	}
	close(listen_fd);
	fclose(st_fd);
retur:
	return 0;
fail:
	close(listen_fd);
	fclose(st_fd);
	return -1;
}

void delay_xx(long t_time)
{
	long i = 0;
	long j = 0;
	for(; i < t_time; i++)
		for(; j < 110; j++)
			;
}

//打印文件内容
//参数为文件指针
void printfile(FILE* fp)
{
	int ret = -1;
	long current = -1;
	char printbuf[128];

	if((current = ftell(fp)) == -1)//保存当前文件指针位置
		return;
	fseek(fp, 0, SEEK_SET);//文件指针指向文件开头
	memset(printbuf, 0, sizeof(printbuf));

	while(fgets(printbuf, 128, fp) != NULL)
	{
		printf("%s", printbuf);
		memset(printbuf, 0, sizeof(printbuf));
	}
	printf("\n");
		fseek(fp, current, SEEK_SET);//恢复文件指针位置
}

//获取系统时间存放在timebuf中
void getTime(char *timebuf)
{
	time_t rawtime;
	struct tm *ttime = NULL;
	memset(timebuf, 0, sizeof(timebuf));
	time(&rawtime);
	ttime = localtime(&rawtime);
	sprintf(timebuf, "(%4d-%02d-%02d %02d:%02d:%02d)",1900+ttime->tm_year, 1+ttime->tm_mon, 
												  ttime->tm_mday, ttime->tm_hour, 
												  ttime->tm_min, ttime->tm_sec);
}
