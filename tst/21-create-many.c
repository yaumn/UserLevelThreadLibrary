#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/time.h>
#include "thread.h"

/* test de plein de create-destroy consécutifs.
 *
 * valgrind doit etre content.
 * la durée du programme doit etre proportionnelle au nombre de threads donnés en argument.
 * jusqu'à combien de threads cela fonctionne-t-il ?
 *
 * support nécessaire:
 * - thread_create()
 * - thread_exit()
 * - thread_join() avec récupération de la valeur de retour
 */

static void * thfunc(void *dummy __attribute__((unused)))
{
  thread_exit(NULL);
}

int main(int argc, char *argv[])
{
  struct timeval tv1, tv2, diff;
  thread_t th;
  int err, i, nb;
  void *res;

  if (argc < 2) {
    printf("argument manquant: nombre de threads\n");
    return -1;
  }

  nb = atoi(argv[1]);
  gettimeofday(&tv1, NULL);
  for(i=0; i<nb; i++) {
    err = thread_create(&th, thfunc, NULL);
    assert(!err);
    err = thread_join(th, &res);
    assert(!err);
    assert(res == NULL);
  }
  gettimeofday(&tv2, NULL);
  timersub(&tv2, &tv1, &diff);
  
  printf("%d threads créés et détruits\n", nb);
  printf("%ld\n", diff.tv_sec * 1000000 + diff.tv_usec);
  return 0;
}
