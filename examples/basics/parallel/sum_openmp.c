/* Build: gcc -std=c11 -fopenmp examples/basics/parallel/sum_openmp.c -o sum-openmp
   Run: ./sum-openmp 2     Expected: sum=36 (also with 1 or 4 threads). */
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  int threads = argc > 1 ? atoi(argv[1]) : 2;
  if (threads < 1 || threads > 64) {
    fprintf(stderr, "threads must be between 1 and 64\n");
    return 1;
  }
  int total = 0;
#pragma omp parallel for num_threads(threads) reduction(+:total)
  for (int i = 1; i <= 8; ++i) {
    total += i;
  }
  printf("sum=%d\n", total);
  return total == 36 ? 0 : 1;
}
