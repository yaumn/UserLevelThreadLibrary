#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <valgrind/valgrind.h>

#include "queue.h"
#include "thread.h"


#define PAGE_SIZE sysconf(_SC_PAGESIZE)
#define STACK_PROTECTION_LENGTH 100
#define STACK_SIZE ((8 * PAGE_SIZE) - 1)
#define TIME_QUANTUM 1000000 // ns


enum state
{
   READY,
   FINISHED
};


struct thread
{
  int id; // Is it necessary ?
  enum state state;
  ucontext_t uc;
  void *retval;
  void *(*func)(void *);
  void *funcarg;
  int valgrind_stack_id;
  int cpu;
  int is_in_yield_queue;

  TAILQ_ENTRY(thread) queue_entries;
  TAILQ_ENTRY(thread) yield_queue_entries;
};


TAILQ_HEAD(thread_queue, thread);

int* threads_ready;

int lib_initialized = 0;
int lib_finalized = 0;
pthread_mutex_t thread_initialized;

int next_thread_id;

struct thread *main_threads;

timer_t timer;
struct itimerspec timerspec;
sigset_t alarm_sigset;
stack_t signal_stack;
int signal_stack_valgrind_id;

pid_t pid;
cpu_set_t **cpu_masks;
pthread_t *cpu_threads;
int *cpu_num_threads;
int num_thread = 0;
pthread_mutex_t mutex_num_thread;
struct thread_queue* cpu_thread_queues;
pthread_mutex_t *cpu_mutex_queues;
struct thread_queue* thread_to_yield_queues;
pthread_mutex_t *thread_to_yield_mutex_queues;
size_t size;
int num_cpus;
__thread int self_cpu = 0; // In a pthread, self_cpu refer to the current cpu
__thread int cpu = 0; // Iterator


/* Called when a son main has nothing to do */
void thread_wait_new_thread();

/* Select the best cpu to handle the new thread */
int thread_select_cpu();

void lock(pthread_mutex_t *m);
void unlock(pthread_mutex_t *m);
void sig_block();
void sig_unblock();


struct thread *new_thread()
{
  struct thread *t = malloc(sizeof(struct thread));
  if (t == NULL) {
    fprintf(stderr, "malloc failed");
    return NULL;
  }

  __atomic_fetch_add(&num_thread, 1, __ATOMIC_SEQ_CST);
  t->id = __atomic_fetch_add(&next_thread_id, 1, __ATOMIC_SEQ_CST);;
  t->uc.uc_stack.ss_size = STACK_SIZE;

  // mprotect must be used on a memory zone returned by mmap,
  // otherwise the result is unspecified
  if ((t->uc.uc_stack.ss_sp = mmap(NULL, t->uc.uc_stack.ss_size + 1 + STACK_PROTECTION_LENGTH,
				   PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
    perror("mmap");
    return NULL;
  };
  
  t->uc.uc_stack.ss_flags = 0;
  t->uc.uc_link = NULL;
  t->valgrind_stack_id = VALGRIND_STACK_REGISTER(t->uc.uc_stack.ss_sp,
  						 t->uc.uc_stack.ss_sp
						 + t->uc.uc_stack.ss_size);

  
  if (mprotect(t->uc.uc_stack.ss_sp + t->uc.uc_stack.ss_size + 1,
	       STACK_PROTECTION_LENGTH, PROT_NONE) == -1) {
    perror("mprotect");
  }
  
  t->func = NULL;
  t->funcarg = NULL;
  t->retval = NULL;
  t->cpu = thread_select_cpu();
  t->is_in_yield_queue = 0;
  
  return t;
}


void free_thread(struct thread *t)
{
  VALGRIND_STACK_DEREGISTER(t->valgrind_stack_id);
  munmap(t->uc.uc_stack.ss_sp, t->uc.uc_stack.ss_size);
  free(t);
}


void handle_signals(int sig)
{
  switch (sig) {
  case SIGALRM:
    thread_yield();
    break;

  case SIGSEGV:; // The ; is here on purpose
    write(2, "Stack overflow detected, killing thread\n", 41);
    thread_exit(NULL);
    break;

  default:
    break;
  }
}


void *thread_handler(void *_cpu)
{
  int cpu = (intptr_t)_cpu;
  // child
  printf("start cpu %d\n", cpu);
  if (getcontext(&main_threads[cpu].uc) == -1) {
    perror("getcontext");
    exit(EXIT_FAILURE);
  }

  main_threads[cpu].id = __atomic_fetch_add(&next_thread_id, 1, __ATOMIC_SEQ_CST);
  main_threads[cpu].state = READY;
  main_threads[cpu].func = NULL;
  main_threads[cpu].funcarg = NULL;
  main_threads[cpu].retval = NULL;
  main_threads[cpu].cpu = cpu;
  main_threads[cpu].valgrind_stack_id = VALGRIND_STACK_REGISTER(main_threads[cpu].uc.uc_stack.ss_sp,
							  main_threads[cpu].uc.uc_stack.ss_sp
							  + main_threads[cpu].uc.uc_stack.ss_size);  

  TAILQ_INSERT_TAIL((cpu_thread_queues + cpu), &main_threads[cpu], queue_entries);
  
  
  
  threads_ready[cpu] = 1; // Notify main that it is ready
  lock(&thread_initialized); // Already locked in init, block until initialization complete
  sig_block();
  unlock(&thread_initialized);
  cpu_threads[cpu] = pthread_self();
  int s = pthread_setaffinity_np(cpu_threads[cpu], size, cpu_masks[cpu]);

  if (s != 0) {
    fputs("cpu asked not available\n", stderr);
    exit(EXIT_FAILURE);
  }

  self_cpu = cpu;
  threads_ready[cpu] = 0;
  thread_wait_new_thread();
  
  return NULL;
}

__attribute__ ((constructor))
void init_lib()
{
  
  printf("Initializing lib : %d\n", getpid());
  
  // For some obscure reason, this function is executed twice on startup
  // so we prevent it from causing memory problems
  if (lib_initialized) {
    return;
  }
  lib_initialized = 1;

  // Mutex to be sure everything is instancied correctly before launching
  pthread_mutex_init(&thread_initialized, NULL);
  lock(&thread_initialized);
  
  next_thread_id = 0;

  /* This part initializes the multi-proc config */
  num_cpus = get_nprocs_conf();
  cpu_masks = malloc(sizeof(cpu_set_t *) * num_cpus);
  cpu_threads = malloc(sizeof(pthread_t) * num_cpus);
  cpu_thread_queues = malloc(sizeof(struct thread_queue) * num_cpus);
  cpu_mutex_queues = malloc(sizeof(pthread_mutex_t) * num_cpus);
  thread_to_yield_queues = malloc(sizeof(struct thread_queue) * num_cpus);
  thread_to_yield_mutex_queues = malloc(sizeof(pthread_mutex_t) * num_cpus);
  cpu_num_threads = malloc(sizeof(int) * num_cpus);
  threads_ready = malloc(sizeof(int) * num_cpus);
  main_threads = malloc(sizeof(struct thread) * num_cpus);

  for (cpu = 0 ; cpu < num_cpus ; cpu++) {
    cpu_num_threads[cpu] = 1;
    threads_ready[cpu] = 0;
    
    TAILQ_INIT((cpu_thread_queues + cpu));
    pthread_mutex_init(cpu_mutex_queues + cpu, NULL);
    TAILQ_INIT((thread_to_yield_queues + cpu));
    pthread_mutex_init(thread_to_yield_mutex_queues + cpu, NULL);  
  }
  
  
  size = CPU_ALLOC_SIZE(num_cpus);
  for (cpu = 0 ; cpu < num_cpus ; cpu ++) {
    cpu_masks[cpu] = CPU_ALLOC(num_cpus);

    if (cpu_masks[cpu] == NULL) {
      fputs("CPU_ALLOC", stderr);
      exit(EXIT_FAILURE);
    }
    
    CPU_ZERO_S(size, cpu_masks[cpu]);
    CPU_SET_S(cpu, size, cpu_masks[cpu]);
  }

  // Le principal main is executed on CPU 0
  cpu_threads[0] = pthread_self();

  // Init signal handler stack
  signal_stack.ss_size = STACK_SIZE;
  signal_stack.ss_sp = malloc(signal_stack.ss_size);
  
  if (signal_stack.ss_sp == NULL) {
    fputs("malloc failed", stderr);
    exit(EXIT_FAILURE);
  }
  
  signal_stack_valgrind_id = VALGRIND_STACK_REGISTER(signal_stack.ss_sp,
						     signal_stack.ss_sp
						     + signal_stack.ss_size);

  
  sigemptyset(&alarm_sigset);
  sigaddset(&alarm_sigset, SIGALRM);
  
  struct sigaction sa;
  sa.sa_handler = handle_signals;
  sa.sa_flags = SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGALRM);

  if (sigaltstack(&signal_stack, NULL) == -1) {
    perror("sigaltstack");
    exit(EXIT_FAILURE);
  }

  if (sigaction(SIGALRM, &sa, NULL) == -1 /*|| sigaction(SIGSEGV, &sa, NULL)*/) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
  
  timerspec.it_value.tv_sec = 0;
  timerspec.it_value.tv_nsec = TIME_QUANTUM;
  timerspec.it_interval.tv_sec = 0;
  timerspec.it_interval.tv_nsec = 0;
  
  if (timer_create(CLOCK_REALTIME, NULL, &timer) == -1) {
    perror("timer_create");
    exit(EXIT_FAILURE);
  }

  if (getcontext(&main_threads[0].uc) == -1) {
    perror("getcontext");
    exit(EXIT_FAILURE);
  }
  
  main_threads[0].id = next_thread_id++;
  main_threads[0].state = READY;
  main_threads[0].func = NULL;
  main_threads[0].funcarg = NULL;
  main_threads[0].retval = NULL;
  main_threads[0].cpu = 0;
  main_threads[0].valgrind_stack_id = VALGRIND_STACK_REGISTER(main_threads[0].uc.uc_stack.ss_sp,
							  main_threads[0].uc.uc_stack.ss_sp
							  + main_threads[0].uc.uc_stack.ss_size);

  
  // Create a child main by CPU
  for (cpu = 1; cpu < num_cpus; cpu++) {
    pthread_create(&cpu_threads[cpu], NULL, thread_handler, (void *)(intptr_t)cpu);
    while (threads_ready[cpu] != 1);
    (*cpu_num_threads) = 1;
  }

  num_thread = num_cpus;

  TAILQ_INSERT_TAIL((cpu_thread_queues + self_cpu), &main_threads[0], queue_entries);
  
  unlock(&thread_initialized); // Launch the other mains
  for (cpu = 1 ; cpu < num_cpus ; cpu++) {
    while (threads_ready[cpu] != 0) {
      pthread_yield();
    }
  }
  
  puts("ALL THREADS INITIALIZED");
}


__attribute__ ((destructor))
void destroy_lib()
{
  sig_block();

  //puts("ENTER DESTRUCTOR");
  if (lib_finalized) {
    return;
  }
  lib_finalized = 1;

  //puts("Waiting pthreads");
  //printf("num_thread : %d\n", num_thread);
  //all_status();

  while(num_thread > num_cpus) {
    sig_unblock();
    pthread_yield();
  }

  __atomic_fetch_sub(&num_thread, 1, __ATOMIC_SEQ_CST);
  for (cpu = 1 ; cpu < num_cpus ; cpu++) {    
    pthread_join(cpu_threads[cpu], NULL);
  }
  
  for (cpu = 0; cpu < num_cpus; cpu++) {
    CPU_FREE(cpu_masks[cpu]);
    pthread_mutex_destroy(cpu_mutex_queues + cpu);
  }
  
  free(main_threads);
  free(cpu_num_threads);
  free(cpu_masks);
  free(cpu_threads);
  free(cpu_thread_queues);
  free(cpu_mutex_queues);
  free(thread_to_yield_queues);
  free(thread_to_yield_mutex_queues);
  free(threads_ready);
  timer_delete(timer);

  VALGRIND_STACK_DEREGISTER(signal_stack_valgrind_id);
  free(signal_stack.ss_sp);

  pthread_mutex_destroy(&mutex_num_thread);
  pthread_mutex_destroy(&thread_initialized);
  //puts("\n END");
}


void call_thread_func()
{
  sig_block();
  struct thread *current = TAILQ_FIRST((cpu_thread_queues + self_cpu));
  sig_unblock();
  thread_exit(current->func(current->funcarg));
}


thread_t thread_self(void)
{
  return TAILQ_FIRST((cpu_thread_queues + self_cpu));
}


int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg)
{
  sig_block();

  struct thread *t = new_thread();
  if (t == NULL) {
    fputs("thread create failed", stderr);
    sig_unblock();
    return -1;
  }
 
  t->func = func;
  t->funcarg = funcarg;

  if (getcontext(&t->uc) == -1) {
    perror("getcontext");
    sig_unblock();
    return -1;
  }

  makecontext(&t->uc, call_thread_func, 0);
  t->state = READY;
  *newthread = t;

  lock(cpu_mutex_queues + t->cpu);

  int first_thread_after_main = 0;
  if (!TAILQ_EMPTY((cpu_thread_queues + t->cpu))) {
    first_thread_after_main = TAILQ_FIRST((cpu_thread_queues + t->cpu))
      == TAILQ_LAST((cpu_thread_queues + t->cpu), thread_queue);
  }
 
  
  (*(cpu_num_threads + t->cpu))++;
  TAILQ_INSERT_TAIL((cpu_thread_queues + t->cpu), t, queue_entries);
  
  unlock(cpu_mutex_queues + t->cpu);
  sig_unblock();
  
  if (first_thread_after_main) {
    timer_settime(timer, 0, &timerspec, NULL);
  }
  
  return 0;
}


int thread_yield(void)
{
  sig_block();
  lock(cpu_mutex_queues + self_cpu);

  if(self_cpu == 0 && !lib_finalized) {
    unlock(cpu_mutex_queues + self_cpu);
    sig_unblock();
    return 0;
  }

  struct thread *self = TAILQ_FIRST((cpu_thread_queues + self_cpu));

  // Return to the while
  if (self == TAILQ_LAST((cpu_thread_queues + self_cpu), thread_queue)) {
    unlock(cpu_mutex_queues + self_cpu);
    sig_unblock();
    return 0;
  }

  struct thread *thread_to_run = NULL;

  lock(thread_to_yield_mutex_queues + self_cpu);
  
  // If there is a specific thread to yield to, place it in the second
  // place of the cpu thread queue
  if (!TAILQ_EMPTY(thread_to_yield_queues + self_cpu)) {
    thread_to_run = TAILQ_FIRST(thread_to_yield_queues + self_cpu);
    TAILQ_REMOVE(thread_to_yield_queues + self_cpu, thread_to_run, yield_queue_entries);
    TAILQ_INSERT_TAIL(thread_to_yield_queues + self_cpu, thread_to_run, yield_queue_entries);

    if (self != thread_to_run) {
      TAILQ_REMOVE(cpu_thread_queues + self_cpu, thread_to_run, queue_entries);
      TAILQ_INSERT_AFTER(cpu_thread_queues + self_cpu, self, thread_to_run, queue_entries);
    }
  }
  
  unlock(thread_to_yield_mutex_queues + self_cpu);

  TAILQ_REMOVE(cpu_thread_queues + self_cpu, self, queue_entries);
  TAILQ_INSERT_TAIL(cpu_thread_queues + self_cpu, self, queue_entries);
  thread_to_run = TAILQ_FIRST(cpu_thread_queues + self_cpu);
  
  unlock(cpu_mutex_queues + self_cpu);
  sig_unblock();
  
  timer_settime(timer, 0, &timerspec, NULL);
  
  if (swapcontext(&self->uc, &thread_to_run->uc) == -1) {
    perror("swapcontext");
    return -1;
  }
  
  return 0;
}


int thread_join(thread_t thread, void **retval)
{
  sig_block();

  struct thread *t = thread;

  lock(thread_to_yield_mutex_queues + t->cpu);
    
  if (t->state != FINISHED && !t->is_in_yield_queue) {
    TAILQ_INSERT_TAIL(thread_to_yield_queues + t->cpu, t, yield_queue_entries);
    t->is_in_yield_queue = 1;
  }
  
  unlock(thread_to_yield_mutex_queues + t->cpu);
  
  while (t->state != FINISHED) {
    thread_yield();
  }
  
  if (retval != NULL) {
    *retval = t->retval;
  }

  // When the return value has been put into retval,
  // the thread can be deleted
  if (t != &main_threads[0]) {
    free_thread(t);
  }
  
  //sig_unblock();

  return 0;
}


void thread_exit(void *retval)
{
  sig_block();

  __atomic_fetch_sub(&num_thread, 1, __ATOMIC_SEQ_CST);
  
  lock(cpu_mutex_queues + self_cpu);
  
  struct thread *self = TAILQ_FIRST(cpu_thread_queues + self_cpu);  
  self->retval = retval;

  cpu_num_threads[self_cpu]--;
  TAILQ_REMOVE(cpu_thread_queues + self_cpu, self, queue_entries); 
  
  if (TAILQ_EMPTY(cpu_thread_queues + self_cpu)) {
    self->state = FINISHED;
    unlock(cpu_mutex_queues + self_cpu);
    pthread_exit(NULL);
  }
  
  struct thread *thread_to_run = TAILQ_FIRST(cpu_thread_queues + self_cpu);

  unlock(cpu_mutex_queues + self_cpu);

  lock(thread_to_yield_mutex_queues + self_cpu);

  // If the finished thread is in the yield queue, we remove it
  if (self->is_in_yield_queue) {
    TAILQ_REMOVE(thread_to_yield_queues + self_cpu, self, yield_queue_entries);
    self->is_in_yield_queue = 0;
  }
  
  unlock(thread_to_yield_mutex_queues + self_cpu);

  self->state = FINISHED;
  
  //  sig_unblock();
  
  setcontext(&thread_to_run->uc);
  perror("setcontext");
  exit(EXIT_FAILURE);
}


int thread_mutex_init(thread_mutex_t *mutex)
{
  sig_block();
  struct thread **owner = (struct thread **)&mutex->owner;
  struct thread *t = NULL;
  __atomic_store(owner, &t, __ATOMIC_SEQ_CST);
  sig_unblock();
  return 0;
}


int thread_mutex_destroy(thread_mutex_t *mutex)
{
  (void)mutex;
  return 0;
}


int thread_mutex_lock(thread_mutex_t *mutex)
{
  sig_block();

  lock(cpu_mutex_queues + self_cpu);
  struct thread **self = &(TAILQ_FIRST(cpu_thread_queues + self_cpu));
  unlock(cpu_mutex_queues + self_cpu);
  
  struct thread **owner = (struct thread **)&mutex->owner;
  struct thread *t = NULL;

  while (!__atomic_compare_exchange(owner, &t, self, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
    lock(thread_to_yield_mutex_queues + t->cpu);

    if (!t->is_in_yield_queue) {    
      TAILQ_INSERT_TAIL(thread_to_yield_queues + t->cpu, t, yield_queue_entries);    
      t->is_in_yield_queue = 1;
    }
    
    unlock(thread_to_yield_mutex_queues + t->cpu);      
    
    t = NULL;
    thread_yield();
  }

  sig_unblock();
  return 0;
}


int thread_mutex_unlock(thread_mutex_t *mutex)
{
  sig_block();

  struct thread **owner = (struct thread **)&mutex->owner;
  struct thread *t = NULL;

  __atomic_store(owner, &t, __ATOMIC_SEQ_CST);
  
  lock(cpu_mutex_queues + self_cpu);
  struct thread *self = TAILQ_FIRST(cpu_thread_queues + self_cpu);
  unlock(cpu_mutex_queues + self_cpu);
       
  lock(thread_to_yield_mutex_queues + self_cpu);

  // If the finished thread is in the yield queue, we remove it
  if (self->is_in_yield_queue) {
    TAILQ_REMOVE(thread_to_yield_queues + self_cpu, self, yield_queue_entries);
    self->is_in_yield_queue = 0;
  }
  
  unlock(thread_to_yield_mutex_queues + self_cpu);
  
  sig_unblock();
  return 0;
}


void thread_wait_new_thread()
{
  while(1) {
    sig_block();
    
    if (cpu_num_threads[self_cpu] <= 1) {
      lock(&mutex_num_thread);

      if (num_thread < num_cpus && self_cpu != 0) {
	//printf("Exit cpu %d, process in queue %d\n", self_cpu, cpu_num_threads[self_cpu]);
	num_thread--;
	unlock(&mutex_num_thread);

	cpu_num_threads[self_cpu]--;
        pthread_exit(NULL);
      }
      
      unlock(&mutex_num_thread);
    }
    
    thread_yield();
  }
}


int thread_select_cpu()
{
  int min = cpu_num_threads[1];
  int min_cpu = 1;

  for (cpu = 2 ; cpu < num_cpus ; cpu++) {
    if (cpu_num_threads[cpu] < min) {
      min = cpu_num_threads[cpu];
      min_cpu = cpu;
    }
  }
  
  return min_cpu;
}


void lock(pthread_mutex_t *m)
{
  pthread_mutex_lock(m);
}


void unlock(pthread_mutex_t *m)
{
  pthread_mutex_unlock(m);
}


void sig_block()
{
  pthread_sigmask(SIG_BLOCK, &alarm_sigset, NULL);
}


void sig_unblock()
{
  pthread_sigmask(SIG_UNBLOCK, &alarm_sigset, NULL);
}
