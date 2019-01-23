#ifndef __MODEBUS_TCP_H__
#define __MODEBUS_TCP_H__

#define MODBUS_READ_SINGLE_REGISTERS	0x03
#define MODBUS_WRITE_SINGLE_REGISTER	0x06
#define MODBUS_WRITE_MULTIPLE_REGISTERS	0x10

struct md_tcp_ctx {
	int fd;
	uint8_t *(*recv)(struct md_tcp_ctx *ctx, uint8_t id, uint8_t func, uint8_t *rbuf, uint16_t bufsiz);
	int (*send)(struct md_tcp_ctx *ctx, uint8_t id, uint8_t func, uint16_t addr, uint8_t cnts, uint8_t *dat);
	void (*destroy)(struct md_tcp_ctx *ctx);
};

struct mdap_header {
	uint8_t session_hi;
	uint8_t session_lo;
	uint8_t protocol_hi;
	uint8_t protocol_lo;
	uint8_t len_hi;
	uint8_t len_lo;
};

struct md_tcp_request {
	struct mdap_header mdap;
	uint8_t id;
	uint8_t func;
	uint8_t addr[2]; // 0:high, 1:low
	uint8_t data[1]; // data[0]: read---bytes to read, write: single write--data, multiple write--registers to write
};

struct md_tcp_response {
	struct mdap_header mdap;
	uint8_t id;
	uint8_t func;
	uint8_t cnt; // bytes
	uint8_t data[1];
};
typedef struct md_tcp_request md_request_t;
typedef struct md_tcp_response md_response_t;

int md_tcp_init(struct md_tcp_ctx *ctx, const char *server, uint16_t port);
void md_tcp_destroy(struct md_tcp_ctx *ctx);

uint8_t *md_tcp_recv(struct md_tcp_ctx *ctx, uint8_t id, uint8_t func, uint8_t *rbuf, uint16_t bufsiz);
int md_tcp_send(struct md_tcp_ctx *ctx, uint8_t id, uint8_t func, uint16_t addr, uint8_t cnts, uint8_t *dat);

#endif
