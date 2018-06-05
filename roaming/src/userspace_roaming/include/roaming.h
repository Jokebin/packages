#ifndef __ROAMING_H__
#define __ROAMING_H__

#include <libev/ev.h>
#include <sys/socket.h>
#include <libubus.h>

#include "list.h"
#include "msg_proto.h"

#ifndef container_of
#define container_of(ptr, type, member) (			\
	(type *)( (char *)ptr - offsetof(type,member) ))
#endif

#define assertion( code , condition ) do { if ( !(condition) ) { cleanup_all( code ); } }while(0)

typedef enum {
	M_ROAMING = 0,
	M_DHCP,
	M_UNKNOWN,
} mtype;

struct message {
	mtype type;
	u8 *buf;
};

void cleanup();
void unix_msg_cb(EV_P_ ev_io *io, int e);
void remote_msg_cb(EV_P_ ev_io *io, int e);
void udp_msg_cb(EV_P_ ev_io *io, int e);
void router_timer_cb(EV_P_ ev_timer *w, int e);
void client_timer_cb(EV_P_ ev_timer *w, int e);
void new_gw_router_cb(EV_P_ ev_timer *w, int e);
void del_gw_router_cb(EV_P_ ev_timer *w, int e);
void client_get_timer_cb(EV_P_ ev_timer *w, int e);
void client_periodic_update_cb(EV_P_ ev_periodic *w, int e);

#endif
