CC=gcc

AR=ar

CFLAGS += -DHAVE_STDARG_H

src=$(wildcard *c)
obj=$(patsubst %.c,%.o,$(src))

libhttp.la: $(obj)
	$(AR) rcs $@ $^

%.o: %.c %.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm *.la *.o *.lo -rf
