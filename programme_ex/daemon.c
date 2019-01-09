#include <stdio.h>
#include <stdlib.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <string.h>
#include <time.h>

#include <assert.h>

#define ERROR_EXIT(m) \
do\
{\
	perror(m);\
	exit(EXIT_FAILURE);\
}\
while(0)


void mydaemon(void)
{
	pid_t pid;
	int fd, i, nfiles;
	struct rlimit rl;

	pid = fork();
	if(pid < 0) 
		ERROR_EXIT("fork failed");

	if(pid > 0)
		exit(0);

	if(-1 == setsid())
		ERROR_EXIT("setsid failed");

	pid = fork();
	if(pid < 0)
		ERROR_EXIT("fork failed");

	if(pid > 0)
		exit(0);

#ifdef RLIMIT_NOFILE
	if(-1 == getrlimit(RLIMIT_NOFILE, &rl))
		ERROR_EXIT("getrlimit failed");

	nfiles = rl.rlim_cur = rl.rlim_max;
	if(-1 == setrlimit(RLIMIT_NOFILE, &rl))
		ERROR_EXIT("setrlimit failed");

	for(i=3; i<nfiles; i++) {
		close(i);
	}
#endif
	
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	open("/dev/null", O_RDONLY);
	open("/dev/null", O_RDWR);
	open("/dev/null", O_RDWR);

	chdir("/");
	umask(0);
}


int main(int argc, char *argv[])
{
	time_t t;
	int fd, i;

	mydaemon();
	fd = open("./mydaemon.log", O_RDWR|O_CREAT, 0644);
	if(fd < 0)
		ERROR_EXIT("open ./mydaemon.log failed");

	for(i=0; i<3; i++) {
		t = time(0);
		char *buf = asctime(localtime(&t));
		write(fd, buf, strlen(buf));
		sleep(10);
	}
	close(fd);

	return 0;
}
