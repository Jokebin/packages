BUILD_DIR ?= $(shell pwd)
INSTALL_DIR ?= $(BUILD_DIR)
PRO_EXE ?= $(INSTALL_DIR)/read_pcap

SRCS=$(wildcard $(BUILD_DIR)/*.c)
OBJS=$(patsubst %.c,%.o, $(SRCS))

#LDFLAGS=-L./lib -L/work/xubin/local/lib -L/work/xubin/local/libmysql/lib/mysql -lmysqlclient -lpcap -lxml2 -lm -ljson-c -lz
#CFLAGS=-I./include/ -I/work/xubin/local/libmysql/include/mysql -I/work/xubin/local/include/ -I./include/libxml2 -g2

LDFLAGS=-L./lib -L/work/xubin/local/lib -lpthread -lpcap -lxml2 -lm -ljson-c -lz
CFLAGS=-I./include/ -I/work/xubin/local/include/ -I./include/libxml2 -g2

$(PRO_EXE):$(OBJS)
	@$(CC) $(OBJS) $(LDFLAGS) -o $@
	@rm -rf $(OBJS) ztest ./json/*.json

$(OBJS):%.o:%.c
	@$(CC) -c $(CFLAGS) $< -o $@

xmltest: xmltest.c
	$(CC) xmltest.c -o $@ $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf $(OBJS) $(PRO_EXE) xmltest  ./json/*.json
