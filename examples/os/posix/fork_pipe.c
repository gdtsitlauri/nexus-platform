/* EY517 Linux lab: processes. The parent forks three children; each child computes a partial sum and sends it
 * through its own pipe; the parent waits for every child (no zombies) and prints the total (expected 5050). */
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  int fds[3][2];
  pid_t pids[3];
  for (int k = 0; k < 3; ++k) {
    if (pipe(fds[k]) != 0) return 1;
    pids[k] = fork();
    if (pids[k] < 0) return 1;
    if (pids[k] == 0) {                 /* child k sums its third of 1..100 */
      close(fds[k][0]);
      long sum = 0;
      for (int i = 1; i <= 100; ++i) if (i % 3 == k) sum += i;
      if (write(fds[k][1], &sum, sizeof sum) != (ssize_t)sizeof sum) _exit(1);
      close(fds[k][1]);
      _exit(0);
    }
    close(fds[k][1]);
  }
  long total = 0;
  for (int k = 0; k < 3; ++k) {
    long part = 0;
    if (read(fds[k][0], &part, sizeof part) != (ssize_t)sizeof part) return 1;
    total += part;
    int status = 0;
    waitpid(pids[k], &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return 1;
  }
  printf("total %ld\n", total);
  return total == 5050 ? 0 : 1;
}
