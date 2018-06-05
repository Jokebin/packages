#ifndef __LOG_H__
#define __LOG_H__

//#define NDEBUG
#include <assert.h>
#include <syslog.h>

#define PROGROM_NAME	"roaming"
#define SYSLOG_FACILITY	LOG_DAEMON

#define LOG(level, format...) _log(PROGROM_NAME, __LINE__, level, format)

#ifndef NDEBUG
#define DEBUG(info)	LOG(LOG_DEBUG, info);
#else
#define DEBUG(info)
#endif

void _log(const char *program, int line, int level, const char *format, ...);

#endif
