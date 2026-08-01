// The smallest useful ZRay example: one region around one loop.
//
// Build it through the four-step pipeline in the README, then run it and read
// the `Entry type = Region` row of zray_application_stats.csv. The loop does
// n 8-byte loads and n 8-byte stores, so Read Bytes and Written Bytes should
// both land near n * 8.

#include "zray.h"
#include <cstdlib>

int main()
{
    const int n = 100000;
    double *a = (double *)malloc(n * sizeof(double));
    double *b = (double *)malloc(n * sizeof(double));
    for (int i = 0; i < n; i++) b[i] = i;

    ZRAY_BEGIN(10);
    for (int i = 0; i < n; i++)
        a[i] = b[i] * 2.0;
    ZRAY_END(10);

    return a[n - 1] == 0.0;
}
