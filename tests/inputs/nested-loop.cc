#include "zray.h"

volatile long sink;

int main() {
  ZRAY_BEGIN(1);
  for (int i = 0; i < 3; ++i) {
    sink += i;
    for (int j = 0; j < 5; ++j)
      sink += j;
  }
  ZRAY_END(1);
  return 0;
}
