#include "zray.h"
#include <cstdio>
#include <xray/xray_log_interface.h>
#include <random>
#include <chrono>

//#define TSC_PROFILE

int main()
{
    long sum = 0;

    auto start = std::chrono::high_resolution_clock::now();

    unsigned long lo, hi, tscValStart, tscValEnd, cycles, cycleTotal=0;
    unsigned long iterations = 1000000000;

    for(int i = 0; i < iterations; i++)
    {
#ifdef TSC_PROFILE
        asm("rdtsc" : "=a" (lo), "=d" (hi));
        tscValStart = lo | (hi<<32);
#endif

ZRAY_BEGIN(7);
        sum += rand()%2;
ZRAY_END(7);

        sum += rand()%2;

        sum += rand()%2;

        sum += rand()%2;

        sum += rand()%2;

        sum += rand()%2;

        sum += rand()%2;

        sum += rand()%2;

#ifdef TSC_PROFILE
        asm("rdtsc" : "=a" (lo), "=d" (hi));
        tscValEnd = lo | (hi<<32);
        cycles = tscValEnd - tscValStart;
        cycleTotal += cycles;
#endif
    }

#ifdef TSC_PROFILE
    printf("TSC_DATA\n");
    printf("Cycle total: %lu\n", cycleTotal);
    printf("Average: %lu\n", cycleTotal/iterations);
#endif

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end-start;
    //printf("Duration %lf\n", diff.count());
    //printf("Sum is %ld\n.", sum);

    return sum;
}
