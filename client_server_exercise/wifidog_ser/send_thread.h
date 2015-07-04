#ifndef __SEND_THREAD_H__
#define __SEND_THREAD_H__

#include "service.h"

#define CLIENT_ROUTER "192.168.1.21"
#define CLIENT_PORT 50001
#define AUTH_OK_MSG "hotdog"
#define AUTH_OK "allow"
#define AUTH_FAILED "deny"

void send_thread_func(mydata *arg);

#endif
