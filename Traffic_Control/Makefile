CC := mips-openwrt-linux-gcc
SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,%.o,$(SRCS))
CFLAGS := -O2 -I/work/xubin/openwrt/staging_dir/target-mips_34kc_uClibc-0.9.33.2/usr/include -I/work/xubin/openwrt/staging_dir/target-mips_34kc_uClibc-0.9.33.2/include -I/work/xubin/openwrt/staging_dir/toolchain-mips_34kc_gcc-4.8-linaro_uClibc-0.9.33.2/usr/include -I/work/xubin/openwrt/staging_dir/toolchain-mips_34kc_gcc-4.8-linaro_uClibc-0.9.33.2/include 

mtc:$(SRCS)
	$(CC) -o $@ $(SRCS) -lpthread

.PHONY : clean
clean :
	rm -rf *.o *.gch mtc
