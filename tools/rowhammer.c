/*
 * gcc -O2 rowhammer.c -o rowhammer
 */

char pad[1024];
long long foo;
char pad2[1024];

void main(void)
{
	long long i;
	asm volatile(
		"mov $foo, %%edi \n\
			clflush (%%edi)" ::: "%edi");
	
	for (i=0; i<1000000000; i++) {
#if 1
		asm volatile(
			"mov $foo, %%edi \n\
			movnti %%eax, (%%edi)" ::: "%edi");
#endif

		// asm volatile( "" );
	}
}
