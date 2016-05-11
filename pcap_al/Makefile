BUILD_DIR ?= $(shell pwd)
INSTALL_DIR ?= $(BUILD_DIR)
PRO_EXE ?= $(INSTALL_DIR)/read_pcap

SRCS=$(wildcard $(BUILD_DIR)/*.c)
OBJS=$(patsubst %.c,%.o, $(SRCS))

LDFLAGS=-L/home/xbin/local/lib -lpcap -L./lib -lxml2
CFLAGS=-I/home/xbin/local/include -I./include/libxml2

$(PRO_EXE):$(OBJS)
	@echo "$(OBJS)"
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJS):%.o:%.c
	@echo $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@ -g

xmltest: xmltest.c
	$(CC) xmltest.c $(CFLAGS) $(LDFLAGS) -o $@

clean:
	rm -rf $(OBJS) $(PRO_EXE) xmltest ./data/*.xml
