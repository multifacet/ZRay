#include "zray.h"
#include <cstdio>
#include <random>
#include <chrono>

int add()
{
    int sum = 0;
ZRAY_BEGIN(5);
    for(int i = 0; i < 15; i++)
    {
            sum += rand()%2;
    }
ZRAY_END(5);
    return sum;
}

int main()
{
    long sum = 0;
    float fSum = 0;
ZRAY_BEGIN(7);
#if 1
    for(int i = 0; i < 10; i++)
    {

        fSum += i*1.0;
        sum += fSum + rand()%2;
        add();
    }
#endif
#if 1
    for(int i = 0; i < 20; i++)
    {
        fSum += i*1.0;
        sum += fSum + rand()%2;
        add();
    }
#endif
ZRAY_END(7);
    
    // Deliberately outside any region: this loop should not appear in the
    // per-region totals.
    for(int i = 0; i < 13; i++)
    {
            sum += rand()%2;
    }

    unsigned long lo, hi;
    asm("rdtsc" : "=a" (lo), "=d" (hi) );
    printf("%lu\n", lo | (hi<<32));
    asm("rdtsc" : "=a" (lo), "=d" (hi) );
    printf("%lu\n", lo | (hi<<32));

    return sum;
}
