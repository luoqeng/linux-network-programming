#include "mempool.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define TEST_BYTES 1024 * 30

int main()
{
	mem_pool objMemPoll;
	srand(time(NULL));

	int n;
	for (int i = 0; i < 90000000; ++i)
	{
		// ��1Ϊ�˱������n=0�����
		n = (rand() % TEST_BYTES) * sizeof(char) + 1;
#if 0
		char *p = (char*)objMemPoll.allocate(n); 
		objMemPoll.deallocate(p, n);
#else
		char *p = (char*)malloc(n); 
		free(p);
#endif
	}
	
	return 0;
}

