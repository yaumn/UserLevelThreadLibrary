#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "thread.h"


/* Somme.
 *
 * la durée doit être proportionnel à la valeur du résultat.
 * valgrind doit être content.
 * jusqu'à quelle valeur cela fonctionne-t-il ?
 *
 * support nécessaire:
 * - thread_create()
 * - thread_join() avec récupération de la valeur de retour
 * - retour sans thread_exit()
 */

size_t size;
int nb_threads;  
int *t;
unsigned long sum = 0;


static void * sum_part(void * a) {
  unsigned long res = 0;
  int id = (intptr_t)a;
  const size_t subsize = (size / nb_threads);
  
  for (size_t i = id * subsize ; (i < (id + 1) * subsize) && (i < size) ; i++) {
    res += t[i];
  }

  __atomic_add_fetch(&sum, res, __ATOMIC_SEQ_CST);

  return NULL;
}


int main(int argc, char *argv[])
{
  struct timeval tv1, tv2, diff;
  thread_t *threads = NULL;
  int err;
  void *res;
  int seed;
  
  if (argc < 3) {
    printf("argument manquant: nombre de threads et taille du tableau\n");
    return -1;
  }

  nb_threads = atoi(argv[1]);
  threads = malloc(nb_threads * sizeof(thread_t));
  size = atoi(argv[2]);
  seed = time(NULL);
  
  srand(seed);

  t = malloc(size * sizeof(int));
  for (size_t i = 0 ; i < size ; i++) {
    t[i] = rand() % 10;
  }

  gettimeofday(&tv1, NULL);
  
  for (int i = 0 ; i < nb_threads ; i++) {
    err = thread_create(&threads[i], sum_part, (void*)(intptr_t)i);
    assert(!err);
  }

  for (int i = 0 ; i < nb_threads ; i++) {
    err = thread_join(threads[i], &res);
    assert(!err);
    assert(res == NULL);
  }

  gettimeofday(&tv2, NULL);

  timersub(&tv2, &tv1, &diff);

  printf("Sum = %lu\n", sum);
  printf("%ld\n", diff.tv_sec * 1000000 + diff.tv_usec);

  free(t);
  free(threads);
  
  return 0;   
}
