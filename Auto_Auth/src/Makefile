CC=gcc

auto_src=auto.c
test_src=gethttp.c

auto: $(auto_src)
	$(CC) $^ -pthread -o $@ 

test: $(test_src)
	$(CC) $^ -pthread -o $@ 

all: auto test

clean:
	rm *.o auto test -rf
