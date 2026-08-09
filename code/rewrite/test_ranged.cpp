#include <cstdio>
#include "rmg/randomizer.h"
int main() {
  ra2::rmg::Randomizer rng(0x1234);
  for (int i = 0; i < 5; ++i) rng.Random();
  uint32_t r5 = rng.Random();
  printf("RNG[5] = 0x%08X\n", r5);
  
  ra2::rmg::Randomizer rng2(0x1234);
  for (int i = 0; i < 5; ++i) rng2.Random();
  int width = rng2.RandomRanged(1, 3);
  printf("RandomRanged(1,3) = %d, consumed = %lld\n", width, rng2.call_count() - 5);
  return 0;
}
