#include "zray.h"
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <thread>
#include <vector>

#if defined(GEM5_BUILD) || defined(GEM5_ZRAY_BUILD)
#include "gem5/m5ops.h"
#endif

thread_local int RAND_POS = 0;
const int RAND_SIZE = 956;
char RAND_STR[] = "18042893838469308861681692777171463691519577477934242383357198853861649760492596516649118964142110252023621350490027783368690110252005920448977631967513926136518054015403834263040891721303455736350052115215953682947025671726956429336465782861021530278722862233665123214517406746870313511015139291801979802131563402263572305813691330691125898167105996139320890184566281750111656478042113117622916533773738594844211914544919608413784756898537173457519819735943241497983152038664370112956641318480352641277609114242689801911759956749241873137806862429991709829069961354972815117023052084420925193747708418273363275726603361159126505805750846163262172911006613131433925857114161612484353895939819582200110054519988988141548233367610515434158599036413743440437603137501477171087356426808945117276188994717817806957887093935844917054031918502651752392754147461239920539999321264095060141154967618439933689439477391984210012855636226174969858614693480941956297539";

#define rand() ({                                 \
        int rand_int = *(int *)&(RAND_STR[RAND_POS]); \
        RAND_POS += 4;                                \
        if (RAND_POS == RAND_SIZE)                    \
        RAND_POS = 0;                             \
        rand_int;                                     \
        })

int GLOBAL_SUM = 73;

// https://stackoverflow.com/questions/60514402/llvm-find-load-store-instructions-that-operate-on-heap
// IR does not distinguish between stack/heap access

// Create a linker script to assign stack/heap/global addresses into distinct regions and determine them at runtime?
// https://sourceware.org/binutils/docs/ld/Scripts.html

// https://stackoverflow.com/questions/53380105/how-to-differentiate-stack-heap-addresses-in-llvm-ir-code
// Alternative, classify return values of alloca() as stack, and malloc as heap
// use for(auto x : y->users()) to classify further accesses. May miss some accesses
//
//
//
int workLoop(int _iter, int _power)
{
#ifdef GEM5_BUILD
    printf("dumping stats\n");
    m5_dump_reset_stats(0,0);
#elif GEM5_ZRAY_BUILD
    printf("dumping stats\n");
    m5_dump_reset_stats(0,0);
ZRAY_BEGIN(15);
#else
ZRAY_BEGIN(15);
#endif
    
   // fork();
    size_t sum = 0;
    float sum2 = 0;

    //size_t iter_power = atoi(argv[1]);
    //size_t power = atoi(argv[2]);
    size_t iter_power = _iter; 
    size_t power = _power;
    
    // size_t iter_count = rand()%100;
    size_t iter_count = 1l << iter_power; 

    //#pragma omp parallel
    for (int z = 0; z < iter_count; z++)
    {
        //size_t power = 16;
        int *array_heap = (int *)malloc(4 * (1 << power));
        float array_stack[256];

        for (int i = 0; i < (1 << power); i++)
        {
            array_heap[i] = rand();
        }

        __asm__("#point 1");
        __asm__("#point 2");
        for (int i = 0; i < (1 << power); i++)
        {
            if (10 % 2)
            //if (rand() % 2)
            {
                //size_t limit2 = rand() % 200;
                size_t limit2 = 200;
                for (int k = 0; k < limit2; k++)
                {
                    __asm__("#point 3a");
                    sum += array_heap[i];
                    GLOBAL_SUM += 1;
                    for (int bk = 0; bk < limit2 + 50; bk++)
                    {

                        sum -= array_heap[i];
                        GLOBAL_SUM -= 3;
                    }
                }
            }
            else
            {
                __asm__("#point 3b");
                if (10 % 20)
                //if (rand() % 20)
                {
                    sum2 += 1.5;
                }
                else if(5 % 10)
                //else if(rand() % 10)
                {
                    sum += 3;
                }
                else
                {
                    sum2 += 5;
                    sum += 8;
                }
            }
        }
        __asm__("#point 4");
        __asm__("#point 5");


        for (int i = 0; i < 256; i++)
        {
            array_stack[i] = (rand() % 20) * .12;
        }

        size_t sum_stack = 0;
        //for (int i = 0; i < rand() % 90; i++)
            for (int i = 0; i < 90; i++)
        {
            sum_stack += array_stack[rand() % 256];
        }

        free(array_heap);

    }

    printf("Sum is %d\n", sum);
    printf("Sum2 is %f\n", sum2);
    printf("GLOBAL SUM is %d\n", GLOBAL_SUM);
    printf("sum stack is %d\n", GLOBAL_SUM);

#ifdef GEM5_BUILD 
    m5_dump_reset_stats(0,0);
#elif GEM5_ZRAY_BUILD
ZRAY_END(15);
    m5_dump_reset_stats(0,0);
#else
ZRAY_END(15);
#endif

    return sum;
}

using namespace std;
int main(int argc, char ** argv)
{

    size_t Sf1, Sf2, ThreadCount;
    if (argc != 4)
    {
        printf("Usage: %s SF1 SF2 THREADCOUNT", argv[0]);
        return 1;
    }

    Sf1 = atol(argv[1]);
    Sf2 = atol(argv[2]);
    ThreadCount = atol(argv[3]);

    std::vector<thread*> ThreadList;

    thread * t;
    for(int i = 0; i < ThreadCount; i++)
    {
        t = new thread(workLoop, Sf1, Sf2);
        ThreadList.push_back(t);
    }

    for(int i = 0; i < ThreadCount; i++)
    {
        ThreadList[i]->join();
    }
}
