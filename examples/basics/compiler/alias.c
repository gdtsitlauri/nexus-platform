/* Build: cc examples/basics/compiler/alias.c -o alias-demo
   Run: ./alias-demo   Expected: different objects: 1; same object: 2. */
#include <stdio.h>

int read_after_writes(int *p, int *q) {
  *p = 1;
  *q = 2;
  return *p;  /* cannot replace by 'return 1': p and q might be equal */
}

int main(void) {
  int x = 0, y = 0;
  int different = read_after_writes(&x, &y);
  int same = read_after_writes(&x, &x);
  printf("different objects: %d\nsame object: %d\n", different, same);
  return different == 1 && same == 2 ? 0 : 1;
}
