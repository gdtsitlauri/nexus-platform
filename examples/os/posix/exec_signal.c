/* EY517 Linux lab: exec and signals. A child replaces itself with /bin/echo; the parent installs a SIGUSR1
 * handler, signals itself, and checks that the handler ran exactly once. */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t hits = 0;
static void on_usr1(int sig) { (void)sig; hits++; }

int main(void) {
  pid_t pid = fork();
  if (pid == 0) {
    execl("/bin/echo", "echo", "child replaced by exec", (char *)NULL);
    _exit(127);  /* only if exec failed */
  }
  int status = 0;
  waitpid(pid, &status, 0);
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_usr1;
  sigaction(SIGUSR1, &sa, NULL);
  raise(SIGUSR1);
  printf("signal handled %d time(s)\n", (int)hits);
  return (WIFEXITED(status) && WEXITSTATUS(status) == 0 && hits == 1) ? 0 : 1;
}
