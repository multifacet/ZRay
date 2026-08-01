#include "zray.h"
#include <cstdio>
#include <random>

int main()
{
    long sum = 0;
    float fSum = 0;

ZRAY_BEGIN(7);
    int counter = 0;
    while (counter < 10)
    {
        counter++;

        for (int i = 0; i < 20; i++)
        {
            fSum += i * 1.0;
            sum += fSum + rand() % 2;
            for (int j = 0; j < 10; j++)
            {
                for (int k = 0; k < 5; k++)
                {
                    fSum += i * j * 1.0 * k;
                    sum += fSum + rand() % 2;
                }
            }
        }

        sum = sum * fSum;

        for (int k = 0; k < 25; k++)
        {
            fSum += k * 1.0;
            sum += fSum + rand() % 2;
        }

        for (int i = 0; i < 35; i++)
        {
            sum += i * rand();
            for (int k = 0; k < 5; k++)
            {
                fSum += i * k * 1.0;
                sum += fSum + rand() % 2;
            }
        }
    }
ZRAY_END(7);
}
