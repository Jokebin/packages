#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

extern int foreground;
extern int loglevel;

void _log(const char *program, int line, int level, const char *format, ...)
{
	if(loglevel < level)
		return;

	char buf[28];
	va_list vlist;
	time_t ts;

	time(&ts);

	if(foreground) {
		if (level <= LOG_WARNING) {
			fprintf(stderr, "[%d][%.24s][%u]<%s:%d> ", level, ctime_r(&ts, buf), getpid(),
					program, line);
			va_start(vlist, format);
			vfprintf(stderr, format, vlist);
			va_end(vlist);
			fputc('\n', stderr);
		} else {
			fprintf(stdout, "[%d][%.24s][%u]<%s:%d> ", level, ctime_r(&ts, buf), getpid(),
					program, line);
			va_start(vlist, format);
			vfprintf(stdout, format, vlist);
			va_end(vlist);
			fputc('\n', stdout);
			fflush(stdout);
		}
	} else {
		openlog(program, LOG_PID, SYSLOG_FACILITY);
		va_start(vlist, format);
		vsyslog(level, format, vlist);
		va_end(vlist);
		closelog();
	}
}
