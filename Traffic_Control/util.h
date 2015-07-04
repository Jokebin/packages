#ifndef _UTIL_H_
#define _UTIL_H_

#define MAX_BUF 4096

int get_ip_posfix(const char *ipaddr);
int execute(char *cmd_line, int quiet);
int iptables_do_command(const char *format, ...);
int tc_do_command(const char *format, ...);
int iptables_fw_destroy_mention(const char * table, const char * chain, const char * mention);

#endif /* _UTIL_H_ */
