/* EY517 Linux lab: POSIX threads. Four threads increment a shared counter under a mutex and signal a condition
 * variable when all are done; without the mutex the count would be lost (see examples/os/race.conc). */
#include <pthread.h>
#include <stdio.h>

static long counter = 0;
static int finished = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t all_done = PTHREAD_COND_INITIALIZER;

static void *work(void *arg) {
  (void)arg;
  for (int i = 0; i < 100000; ++i) {
    pthread_mutex_lock(&lock);
    counter++;
    pthread_mutex_unlock(&lock);
  }
  pthread_mutex_lock(&lock);
  if (++finished == 4) pthread_cond_signal(&all_done);
  pthread_mutex_unlock(&lock);
  return NULL;
}

int main(void) {
  pthread_t t[4];
  for (int i = 0; i < 4; ++i) pthread_create(&t[i], NULL, work, NULL);
  pthread_mutex_lock(&lock);
  while (finished < 4) pthread_cond_wait(&all_done, &lock);
  pthread_mutex_unlock(&lock);
  for (int i = 0; i < 4; ++i) pthread_join(t[i], NULL);
  printf("counter %ld\n", counter);
  return counter == 400000 ? 0 : 1;
}
