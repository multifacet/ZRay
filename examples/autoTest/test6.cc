#include "zray.h"
#include <cstdio>
#include <xray/xray_log_interface.h>
#include <random>
#include <chrono>

//#define TSC_PROFILE

int main()
{
    auto start = std::chrono::high_resolution_clock::now();

    unsigned long lo, hi, tscValStart, tscValEnd, cycles, cycleTotal=0;
    unsigned long iterations = 1000000000;

	int * array = (int*) malloc(sizeof(int)*(iterations));
	for (int i = 0; i < (iterations); i++)
	{
		array[i] = rand();
	}

	size_t sum = 0;
	float sum2 = 0;

ZRAY_BEGIN(1);
    for(int i = 0; i < iterations; i++)
    {
#ifdef TSC_PROFILE
        asm("rdtsc" : "=a" (lo), "=d" (hi));
        tscValStart = lo | (hi<<32);
#endif
		if (rand()%2)
		{
			sum += array[i];
		}
		else
		{
			sum2 += 1.5;
		}


#ifdef TSC_PROFILE
        asm("rdtsc" : "=a" (lo), "=d" (hi));
        tscValEnd = lo | (hi<<32);
        cycles = tscValEnd - tscValStart;
        cycleTotal += cycles;
#endif
    }
ZRAY_END(1);

	printf("Sum is %d\n", sum);
	printf("Sum2 is %f\n", sum2);

#ifdef TSC_PROFILE
    printf("TSC_DATA\n");
    printf("Cycle total: %lu\n", cycleTotal);
    printf("Average: %lu\n", cycleTotal/iterations);
#endif

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end-start;
    //printf("Duration %lf\n", diff.count());
    //printf("Sum is %ld\n.", sum);

    return 0;
}
