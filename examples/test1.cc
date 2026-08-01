#include "zray.h"
#include <cstdlib>
#include <iostream>
#include "gem5/m5ops.h"

int RAND_POS = 0;
const int RAND_SIZE = 956;
char RAND_STR[] = "18042893838469308861681692777171463691519577477934242383357198853861649760492596516649118964142110252023621350490027783368690110252005920448977631967513926136518054015403834263040891721303455736350052115215953682947025671726956429336465782861021530278722862233665123214517406746870313511015139291801979802131563402263572305813691330691125898167105996139320890184566281750111656478042113117622916533773738594844211914544919608413784756898537173457519819735943241497983152038664370112956641318480352641277609114242689801911759956749241873137806862429991709829069961354972815117023052084420925193747708418273363275726603361159126505805750846163262172911006613131433925857114161612484353895939819582200110054519988988141548233367610515434158599036413743440437603137501477171087356426808945117276188994717817806957887093935844917054031918502651752392754147461239920539999321264095060141154967618439933689439477391984210012855636226174969858614693480941956297539";

//According to gem5, this function causes 4 load instructions on X86
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
using namespace std;

//g++ on X86 with Power 3, iter 0 : baseline: 504489 loads
// Loops add on ~39 loads with no internals
// 

#define POWER 10 
int main()
{
    size_t sum = 0;
    float sum2 = 0;

    int *array_heap = (int *)malloc(4 * (1 << POWER));
    float array_stack[(1<<POWER)];

    //rand(); // 4 loads
    //array_heap[0] = rand(); //Assignment adds 2 loads (6 total)
    //sum += array_heap[0]; //+3 loads total
    //sum2 += array_stack[0]; //+2

    //g++ default
    //Baseline : 504450
    //Total loads : iter * ((13 * (1<<power)) + (7 * (1<<power)) + 2 * (2 * (1<<power) + 1)) + (2 * (iter) + 1)
    
    //custom clang++ default
    //Baseline : 507330
    
    size_t iter_count = 1 << 1;

    //g++ : (2 * iter + 1)
    //llvm : (
#ifdef GEM5_BUILD
m5_dump_reset_stats(0,0);
#endif

ZRAY_BEGIN(15);
    for (int z = 0; z < iter_count; z++)
    {
        for (int i = 0; i < (1 << POWER); i++) //13 * (1<<power) ; (loop ~= 2*(1<<power) + 1)
        {
            array_heap[i] = rand(); //+7
            array_stack[i] = rand(); //+6
        }

        for (int i = 0; i < (1 << POWER); i++) //7 * (1<<power) 
        {
            sum += array_heap[i]; //+4
            sum2 += array_stack[i]; //+3
        }
    }
ZRAY_END(15);

#ifdef GEM5_BUILD
m5_dump_reset_stats(0,0);
#endif

    //printf("Sum is %ld\n", sum);
    //printf("Sum2 is %f\n", sum2);
    //printf("GLOBAL SUM is %d\n", GLOBAL_SUM);
    //printf("sum stack is %d\n", GLOBAL_SUM);
}
