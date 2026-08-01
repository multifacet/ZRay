#include <cstdio>
#include <random>
#include <chrono>

//#define TSC_PROFILE

void testingFunc(void *arg, size_t size)
{
    printf("Inside custom function\n");
}
float add(float x, float y)
{
    return x + y;
}
int add()
{
    
    int x = 10, y = 10;
    printf("Result is %d\n", x+y);


    return x + y;
}

int main()
{


    long sum = 0;
    float x,y;

    auto start = std::chrono::high_resolution_clock::now();

    unsigned long lo, hi, tscValStart, tscValEnd, cycles, cycleTotal=0;
    unsigned long iterations = 1000000000;

    for(int i = 0; i < iterations; i++)
    {
#ifdef TSC_PROFILE
        asm("rdtsc" : "=a" (lo), "=d" (hi));
        tscValStart = lo | (hi<<32);
#endif

        sum += i;
        //    if(sum % 2 == 0)
        //return sum;
        //#pragma unroll 100
        for(int j = 0; j < 3; j++)
        {
            sum += rand()%2;
            x = sum;
            y = sum*2;
            sum += add(x,y);
        }

            sum += rand()%2;

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
    printf("Cycle total: %lu\n", cycleTotal);
    printf("Average: %lu\n", cycleTotal/iterations);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end-start;
    printf("Duration %lf\n", diff.count());

    sum = 0;
    while(sum < 10)
    {
        sum++;
    }

    printf("Add returned %d\n", add());

    printf("sum is %ld\n.", sum);

    return sum;
}
