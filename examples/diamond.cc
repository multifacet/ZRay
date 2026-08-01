#include "zray.h"
#include <cstdio>
#include <random>
#include <time.h>

int main()
{
    float fSum = 0;
    int iSum = 0;
    srand(time(nullptr));

#if 1
//~3480 int, ~521 fp
ZRAY_BEGIN(10);
    for (int i = 0; i < 100000; i++)
    {
        if (rand() % 2 == 0)
        {
            for (int j = 0; j < 100; j++)
                fSum += 10.3;
        }
        else
        {
            for (int j = 0; j < 500; j++)
                iSum += 10;
        }
    }
ZRAY_END(10);
#endif

// Reports 200 FP instructions if control logic is not accounted for
#if 1
ZRAY_BEGIN(15);
    for (int i = 0; i < 10000; i++)
    {
        int j = rand()%20;
        if (j % 5 == 0)
        {
            fSum += rand() / 1.5; //200 Float ops
        }
        else if(j % 3 == 0)
        {
            fSum += 13; //100 Float ops
        }
        else
        {
            iSum += rand(); //100 Int ops
        }


        if (j%2 == 0)
        {
            for(int k = 0; k < 10; k++)
            {
                fSum += rand() / 1.5; //200 Float ops
            }
        }
        else
        {
            for(int k = 0; k < 20; k++)
            {
                fSum += rand() * 1.5; //200 Float ops
            }
        }
    }
ZRAY_END(15);
#endif
}
