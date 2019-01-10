#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "modbus_tcp.h"

static struct md_tcp_ctx md_ctx = {
	.fd = -1,
	.recv = md_tcp_recv,
	.send = md_tcp_send,
	.destroy = md_tcp_destroy,
};
#define TAG	"modbus_tcp"


int md_tcp_init(struct md_tcp_ctx **ctx, const char *server, uint16_t port)
{
	int ret = -1;
	struct sockaddr_in ser_addr;

	if(md_ctx.fd <= 0) {
		md_ctx.fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
		if(md_ctx.fd < 0) {
			ESP_LOGE(TAG, "Failed to create socket, Error %d", errno);
			return -1;
		}
	}

	ser_addr.sin_family = AF_INET;
	ser_addr.sin_port = htons(port);
	if(inet_pton(AF_INET, server, &ser_addr.sin_addr) != 1) {
		ESP_LOGE(TAG, "Failed to create socket, Error %d", errno);
		return -1;
	}

	ret = connect(md_ctx.fd, (struct sockaddr*)&ser_addr, sizeof(ser_addr));
	if(ret < 0) {
		ESP_LOGE(TAG, "Failed to connect %s:%d, Error %d", server, port, errno);
		return -1;
	}

	*ctx = &md_ctx;
	md_ctx.recv = md_tcp_recv;
	md_ctx.send = md_tcp_send;
	md_ctx.destroy = md_tcp_destroy;

	return 0;
}

void md_tcp_destroy(struct md_tcp_ctx *ctx)
{
	if(ctx->fd > 0) {
		shutdown(ctx->fd, SHUT_RD);
		close(ctx->fd);
		ctx->fd = -1;
	}

	ctx->recv = NULL;
	ctx->send = NULL;
	ctx->destroy = NULL;
}

#define MODBUS_SESSION_HI	0x44
#define MODBUS_SESSION_LO	0x55

// return: -1 error, 1 msg error 
// 			2 new cmd or resquest not handled
// 			0 request has been handled
uint8_t *md_tcp_recv(struct md_tcp_ctx *ctx, uint8_t id, uint8_t func, uint8_t *rbuf, uint16_t bufsiz)
{
	int ret = -1;
	md_response_t *response;

	ret = recv(ctx->fd, rbuf, bufsiz, 0);
	if(ret <= 0) {
		ESP_LOGE(TAG, "recv failed, ERROR: %d", errno);
		return NULL;
	}

	response = (md_response_t*)rbuf;
	if(response->mdap.session_hi != MODBUS_SESSION_HI \
			|| response->mdap.session_lo != MODBUS_SESSION_LO \
			|| response->id != id \
			|| response->func != func) {
		ESP_LOGI(TAG, "msg error");
		return NULL;
	}

	return &response->data[0];
}

// single write: cnts represent bytes to write
// multiple write: cnts represent bytes to write
// read: cnts represent bytes to read
int md_tcp_send(struct md_tcp_ctx *ctx, uint8_t id, uint8_t func, uint16_t addr, uint8_t cnts, uint8_t *dat)
{
	int ret = -1, i = 0;
	uint8_t sbuf[] = {
		MODBUS_SESSION_HI, MODBUS_SESSION_LO,	// session_id
		0, 0,									// protocol
		0, 0x04,								// len
		id,										// id
		func,									// func
		addr >> 8, addr & 0x00FF,				// addr
	};

	md_request_t *request = (md_request_t *)sbuf;
	uint16_t hlen = sizeof(struct mdap_header);
	uint16_t dlen = request->mdap.len_hi << 8 | request->mdap.len_lo;

	switch(func) {
		case MODBUS_WRITE_SINGLE_REGISTER:
			if(cnts != 2)
				return -1;

			request->data[0] = dat[0]; //data high
			request->data[1] = dat[1]; //data low
			dlen += 2;
			break;
		case MODBUS_WRITE_MULTIPLE_REGISTERS:
			request->data[0] = 0; 		//register number high
			request->data[1] = cnts/2;	//register number low
			request->data[2] = cnts;	//bytes to write
			for(i=0; i<cnts; i++) {
				request->data[i+3] = dat[i+2];
			}

			dlen = dlen + 3 + cnts;
			break;
		case MODBUS_READ_SINGLE_REGISTERS:
			if(cnts != 2)
				return -1;

			request->data[0] = 0;		//bytes to read high
			request->data[1] = cnts/2;	//bytes to read low
			dlen = dlen + 2;
			break;
		default:
			break;
	}

	request->mdap.len_hi = dlen >> 8;
	request->mdap.len_lo = dlen & 0xFF;

	ret = send(ctx->fd, sbuf, hlen+dlen, 0);
	if(ret <= 0) {
		ESP_LOGE(TAG, "send failed, ERROR: %d", errno);
		return ret;
	}

	return ret;
}
