#include <stdio.h>
#include <stdlib.h>

void ValueToIp(long value, int *ip) 
{
	int i;

	for (i = 0; i < 4; i++) {
		ip[3 - i] = value % 256;
		value >>= 8;
	}
}

int main(int argc, char **argv)
{
	if(argc < 2) {
		printf("lack arguments\n");
		return -1;
	}

	long value = strtol(argv[1], NULL, 16);
	int ip[4] = {0};

	ValueToIp(value, ip);

	printf("value = %lx\n", value);
	printf("ip = %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

	return 0;
}
