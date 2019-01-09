#include <stdio.h>


void test()
{
	int i;
	for(i=0; i<20; i++) {
		int b=2;
		char c='c';
		char arra[100];

		printf("address of b: %p\n", &b);
		printf("address of c: %p\n", &c);
		printf("address of arra: %p\n", arra);
	}
}


int main(void)
{
	test();

	return 1;
}
