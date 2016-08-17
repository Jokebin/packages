BUILD_DIR ?= $(shell pwd)
INSTALL_DIR ?= $(BUILD_DIR)
PRO_EXE ?= $(INSTALL_DIR)/read_pcap

SRCS=$(wildcard $(BUILD_DIR)/*.c)
OBJS=$(patsubst %.c,%.o, $(SRCS))

LDFLAGS=-L/work/xubin/local/lib -lpcap -L/work/xubin/demo_gerrit/pcap_al/lib -lxml2
CFLAGS=-I/work/xubin/local/include -I/work/xubin/demo_gerrit/pcap_al/include/libxml2 -g2

$(PRO_EXE):$(OBJS)
	@echo "$(OBJS)"
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJS):%.o:%.c
	@echo $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

xmltest: xmltest.c
	$(CC) xmltest.c -o $@ $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf $(OBJS) $(PRO_EXE) xmltest ./data/*.xml
