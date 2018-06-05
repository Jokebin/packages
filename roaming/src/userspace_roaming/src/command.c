#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "log.h"
#include "command.h"

void *safe_malloc(int siz)
{
	char *buf = NULL;
	buf = (char *)malloc(siz);
	if(buf == NULL) {
		LOG(LOG_ERR, "malloc %s", strerror(errno));
		exit(-1);
	}

	memset(buf, 0, siz);

	return buf;
}

char *safe_strdup(const char *s, int len)
{
	char *str;

	str = strndup(s, len);
	if(str == NULL) {
		LOG(LOG_ERR, "strdup %s", strerror(errno));
		exit(-1);
	}

	return str;
}

pid_t safe_fork()
{
	pid_t result;

	result = fork();
	if(result == -1) {
		LOG(LOG_ERR, "Failed to fork: %s.  Bailing out", strerror(errno));
		exit(-1);
	}

	return result;
}

int safe_vasprintf(char **strp, const char *fmt, va_list ap) {
	int retval;

	retval = vasprintf(strp, fmt, ap);

	if (retval == -1) {
		LOG(LOG_ERR, "Failed to vasprintf: %s.  Bailing out", strerror(errno));
		exit (1);
	}
	return (retval);
}

int execute(const char *cmd_line)
{
        int pid,
            status;

        const char *new_argv[4];
        new_argv[0] = "/bin/sh";
        new_argv[1] = "-c";
        new_argv[2] = cmd_line;
        new_argv[3] = NULL;

        pid = safe_fork();
        if (pid == 0) {    /* for the child process:         */
                if (execvp("/bin/sh", (char *const *)new_argv) == -1) {    /* execute the command  */
                        LOG(LOG_WARNING, "execvp(): %s", strerror(errno));
                } else {
                        LOG(LOG_WARNING, "execvp() failed");
                }
                exit(1);
        }

		/* for the parent: wait cmd excuted*/
		waitpid(pid, &status, 0);
		if(WIFEXITED(status)) {
			return (WEXITSTATUS(status));
		} else {
			LOG(LOG_WARNING, "Process PID %d exited unnormally", pid);
			return -1;
		}
}

int do_command(const char *format, ...)
{
	va_list vlist;
	char *cmd;
	int rc = -1;

	va_start(vlist, format);
	safe_vasprintf(&cmd, format, vlist);
	va_end(vlist);

	rc = execute(cmd);

	LOG(LOG_DEBUG, "%s @rc = %d", cmd, rc);
	free(cmd);

	return rc;
}
