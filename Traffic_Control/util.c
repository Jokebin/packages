#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdarg.h>
#include <errno.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#if defined(__NetBSD__)
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <util.h>
#endif

#ifdef __linux__
#include <netinet/in.h>
#include <net/if.h>
#endif

#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include "util.h"
#include "traffic_control.h"

int get_ip_posfix(const char *ipaddr)
{
	char *p = NULL;
	if(!ipaddr)
		return -1;
	p = strrchr(ipaddr, '.');
	if(!p || !(p + 1))
		return -1;
	return atoi(p+1);
}

/** Fork a child and execute a shell command.
 * The parent process waits for the child to return,
 * and returns the child's exit() value.
 * @return Return code of the command
 */
int
execute(char *cmd_line, int quiet)
{
	int status, retval;
	pid_t pid, rc;
	struct sigaction sa, oldsa;
	const char *new_argv[4];
	new_argv[0] = "/bin/sh";
	new_argv[1] = "-c";
	new_argv[2] = cmd_line;
	new_argv[3] = NULL;

	/* Temporarily get rid of SIGCHLD handler (see gateway.c), until child exits.
	 * Will handle SIGCHLD here with waitpid() in the parent. */
//	printf("Setting default SIGCHLD handler SIG_DFL\n");
	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
	if (sigaction(SIGCHLD, &sa, &oldsa) == -1) {
		printf("sigaction() failed to set default SIGCHLD handler: %s\n", strerror(errno));
	}

	pid = fork();

	if (pid == 0) {    /* for the child process:         */

		if (quiet) close(2); /* Close stderr if quiet flag is on */
		if (execvp("/bin/sh", (char *const *)new_argv) == -1) {    /* execute the command  */
			printf("execvp(): %s\n", strerror(errno));
		} else {
			printf("execvp() failed\n");
		}
		exit(1);

	} else {        /* for the parent:      */
//		printf("Waiting for PID %d to exit\n", (int)pid);
		do {
			rc = waitpid(pid, &status, 0);
			if(rc == -1) {
				if(errno == ECHILD) {
//					printf("waitpid(): No child exists now. Assuming normal exit for PID %d\n", (int)pid);
					retval = 0;
				} else {
					printf("Error waiting for child (waitpid() returned -1): %s\n", strerror(errno));
					retval = -1;
				}
				break;
			}
			if(WIFEXITED(status)) {
//				printf("Process PID %d exited normally, status %d\n", (int)rc, WEXITSTATUS(status));
				retval = (WEXITSTATUS(status));
			}
			if(WIFSIGNALED(status)) {
//				printf("Process PID %d exited due to signal %d\n", (int)rc, WTERMSIG(status));
				retval = -1;
			}
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));

//		printf("Restoring previous SIGCHLD handler\n");
		if (sigaction(SIGCHLD, &oldsa, NULL) == -1) {
			printf("sigaction() failed to restore SIGCHLD handler! Error %s\n", strerror(errno));
		}

		return retval;
	}
}

int
iptables_do_command(const char *format, ...)
{
	va_list vlist;
	char *fmt_cmd, *cmd;
	int rc;
	int i;
	int fw_quiet = 1;

	va_start(vlist, format);
	vasprintf(&fmt_cmd, format, vlist);
	va_end(vlist);

	asprintf(&cmd, "iptables %s", fmt_cmd);

	free(fmt_cmd);

	printf("Executing command: %s\n", cmd);

	for (i = 0; i < 5; i++) {
		rc = execute(cmd, fw_quiet);
		if (rc == 4) {
			/* iptables error code 4 indicates a resource problem that might
			 * be temporary. So we retry to insert the rule a few times. (Mitar) */
			sleep(1);
		} else {
			break;
		}
	}
	if(!fw_quiet && rc != 0) {
		printf("Nonzero exit status %d from command: %s\n", rc, cmd);
	}

	free(cmd);

	return rc;
}

int
tc_do_command(const char *format, ...)
{
	va_list vlist;
	char *fmt_cmd, *cmd;
	int rc;
	int fw_quiet = 1;

	va_start(vlist, format);
	vasprintf(&fmt_cmd, format, vlist);
	va_end(vlist);

	asprintf(&cmd, "tc %s", fmt_cmd);

	free(fmt_cmd);

	printf("Executing command: %s\n", cmd);
	rc = execute(cmd, fw_quiet);
	if(!fw_quiet && rc != 0) {
		printf("Nonzero exit status %d from command: %s\n", rc, cmd);
	}

	free(cmd);

	return rc;
}

/*
 * Helper for iptables_fw_destroy
 * @param table The table to search
 * @param chain The chain in that table to search
 * @param mention A word to find and delete in rules in the given table+chain
 */
int
iptables_fw_destroy_mention(
		const char * table,
		const char * chain,
		const char * mention
		) {
	FILE *p = NULL;
	char *command = NULL;
	char *command2 = NULL;
	char line[MAX_BUF];
	char rulenum[10];
	char *victim = strdup(mention);
	int deleted = 0;

	printf("Attempting to destroy all mention of %s from %s.%s\n", victim, table, chain);
	asprintf(&command, "iptables -t %s -L %s -n --line-numbers -v", table, chain);

	if ((p = popen(command, "r"))) {
		/* Skip first 2 lines */
		while (!feof(p) && fgetc(p) != '\n');
		while (!feof(p) && fgetc(p) != '\n');
		/* Loop over entries */
		while (fgets(line, sizeof(line), p)) {
			/* Look for victim */
			if (strstr(line, victim)) {
				/* Found victim - Get the rule number into rulenum*/
				if (sscanf(line, "%9[0-9]", rulenum) == 1) {
					/* Delete the rule: */
					printf("Deleting rule %s from %s.%s because it mentions %s\n", rulenum, table, chain, victim);
					asprintf(&command2, "-t %s -D %s %s", table, chain, rulenum);
					iptables_do_command(command2);
					free(command2);
					deleted = 1;
					/* Do not keep looping - the captured rulenums will no longer be accurate */
					break;
				}
			}
		}
		pclose(p);
	}

	free(command);
	free(victim);

	if (deleted) {
		/* Recurse just in case there are more in the same table+chain */
		iptables_fw_destroy_mention(table, chain, mention);
	}

	return (deleted);
}

