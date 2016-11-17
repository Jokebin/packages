#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdarg.h>

#define BUFF_SIZ 1024
#define MAX_CHAIN_NAME_LEN 32
#define IPTABLES_MARK_MASK 0xff00

int execute(const char *cmd_line)
{
        int pid,
            status;

        const char *new_argv[4];
        new_argv[0] = "/bin/sh";
        new_argv[1] = "-c";
        new_argv[2] = cmd_line;
        new_argv[3] = NULL;

        pid = fork();
        if (pid == 0) {    /* for the child process:         */
                if (execvp("/bin/sh", (char *const *)new_argv) == -1) {    /* execute the command  */
                        dprintf("execvp(): %s\n", strerror(errno));
                } else {
                        dprintf("execvp() failed\n");
                }
                exit(1);
        }

		/* for the parent:      */
		//dprintf("Waiting for PID %d to exit\n", pid);
		waitpid(pid, &status, 0);
		if(WIFEXITED(status)) {
			return (WEXITSTATUS(status));
		} else {
			dprintf("Process PID %d exited unnormally", pid);
			return -1;
		}
}

int do_command(const char *format, ...)
{
	va_list vlist;
	char *cmd;
	int rc = -1;

	va_start(vlist, format);
	vasprintf(&cmd, format, vlist);
	va_end(vlist);
	
	rc = execute(cmd); 

	free(cmd);
	dprintf("%s ############rc = %d", cmd, rc);

	return rc;
}

int clear_ipts()
{
	FILE *p = NULL;	
	char *chain = "mangle";
	char *flags = "MultiWan";
	char rbuff[BUFF_SIZ] = {0};
	char cmd[BUFF_SIZ] = {0};
	char cmd1[BUFF_SIZ] = {0};

	//empty all chains of MultiWan
	snprintf(cmd, sizeof(cmd), "iptables -S -t %s | grep \"\\-j %s\"", chain, flags);
	if((p = popen(cmd, "r"))) {
		while(fgets(rbuff, sizeof(rbuff), p)) {
			rbuff[1] = 'D';
			rbuff[strlen(rbuff) - 1] = '\0';
			snprintf(cmd1, sizeof(cmd1), "iptables -t %s %s", chain, rbuff);
			printf("%s\n", cmd1);
			do_command(cmd1);
		}
		pclose(p);
	}
	p = NULL;

	//delete all chains of MultiWan
	snprintf(cmd, sizeof(cmd), "iptables -S -t %s | grep \"\\-N %s\" | awk \'{print $2}\'",\
			chain, flags);
	if((p = popen(cmd, "r"))) {
		while(fgets(rbuff, sizeof(rbuff), p)) {
			rbuff[strlen(rbuff) - 1] = '\0';
			snprintf(cmd1, sizeof(cmd1), "iptables -t %s -F %s && iptables -t %s -X %s", \
					chain, rbuff, chain, rbuff);
			printf("%s\n", cmd1);
			do_command(cmd1);
		}
		pclose(p);
	}

	return 0;
}

int clear_iprules()
{
	FILE *p = NULL;
	char rbuff[BUFF_SIZ] = {0};
	char cmd[BUFF_SIZ] = {0};
	char cmd1[BUFF_SIZ] = {0};

	//delete all chains of MultiWan
	snprintf(cmd, sizeof(cmd), "ip rule | grep \"%x\" | awk -F \':\' \'{print $1}\'",\
			IPTABLES_MARK_MASK);
	if((p = popen(cmd, "r"))) {
		while(fgets(rbuff, sizeof(rbuff), p)) {
			rbuff[strlen(rbuff) - 1] = '\0';
			snprintf(cmd1, sizeof(cmd1), "ip rule del prio %s", rbuff);
			printf("%s\n", cmd1);
			do_command(cmd1);
		}
		pclose(p);
		return 0;
	}

	return -1;
}

int main(int argc, char **argv)
{
	clear_ipts();
	clear_iprules();

	return 0;
}
