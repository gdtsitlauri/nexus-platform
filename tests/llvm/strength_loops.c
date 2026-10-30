/* Input for the NexusPasses LLVM plugin test: multiplications, unsigned divisions and remainders by powers of
 * two for nexus-strength, and a 100 x 10 loop nest for nexus-loops. */
#include <stdio.h>

unsigned scale(unsigned x) { return x * 8u + x / 4u + x % 16u + 32u * x; }

int main(void) {
  unsigned sum = 0;
  for (unsigned i = 0; i < 100u; ++i) {
    sum += scale(i * 3u + 1u);
    for (unsigned j = 0; j < 10u; ++j) {
      sum ^= j * 16u + (sum % 64u);
    }
  }
  printf("%u\n", sum);
  return 0;
}
