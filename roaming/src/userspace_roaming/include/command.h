#ifndef __COMMAND_H__
#define __COMMAND_H__

pid_t safe_fork();
void *safe_malloc(int siz);
char *safe_strdup(const char *s, int len);
int do_command(const char *format, ...);

#endif
