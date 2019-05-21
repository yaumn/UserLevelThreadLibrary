#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/time.h>
#include "thread.h"

/* test de plein de switch pendant que N-1 threads sont bloqués dans join
 *
 * la durée du programme doit etre proportionnelle au nombre de threads et de yields donnés en argument
 *
 * support nécessaire:
 * - thread_create()
 * - thread_yield() depuis ou vers le main
 * - retour sans thread_exit()
 * - thread_join() avec récupération de la valeur de retour
 */

static int nbyield;
struct timeval diff;
  
static void * thfunc(void *_nbth)
{
  unsigned long nbth = (unsigned long) _nbth;
  if ((unsigned long) nbth > 0) {
    thread_t th;
    int err;
    void *res;
    err = thread_create(&th, thfunc, _nbth-1);
    assert(!err);
    err = thread_join(th, &res);
    assert(!err);
    assert(res == _nbth-1);
  } else {
    int i;
    struct timeval tv1, tv2;
    gettimeofday(&tv1, NULL);
    for(i=0; i<nbyield; i++)
      thread_yield();
    gettimeofday(&tv2, NULL);
    timersub(&tv2, &tv1, &diff);
    printf("%d yield avec plein de threads dans join: %ld us\n",
	   nbyield, diff.tv_sec * 1000000 + diff.tv_usec);
  }
  return _nbth;
}

int main(int argc, char *argv[])
{
  unsigned long nbth;

  if (argc < 3) {
    printf("arguments manquants: nombre de threads, puis nombre de yield\n");
    return -1;
  }

  nbth = atoi(argv[1]);
  nbyield = atoi(argv[2]);

  thfunc((void*) nbth);

  printf("%ld threads créés et détruits\n", nbth);
  printf("%ld\n", diff.tv_sec * 1000000 + diff.tv_usec);
  return 0;
}
