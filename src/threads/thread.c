#include "threads/thread.h"
//24415
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "../lib/kernel/bitmap.h"
#include "filesys/file.h"
#include "filesys/bcache.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#endif

#include "filesys/file.h"
#include "filesys/directory.h"

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* List of all the dead processes's EXIT_THREAD. An EXIT_THREAD structure
   is added to this list after the process's memory has been freed */
static struct list dead_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void schedule_tail (struct thread *prev);
void fd_mem_free (void);
void thread_wakeup (void);
static tid_t allocate_tid (void);

void filesys_thread (void *);
void filesys_readahead_thread (void *);
/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&dead_list);
  
  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
 
  /* Start the readahead thread */
  thread_create ("Filesys_readahead", PRI_DEFAULT, filesys_readahead_thread, (void *) NULL);     
  
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
struct thread *
thread_create_blocked (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct intr_frame *if_;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  enum intr_level old_level;
	
  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return NULL;

  /* Initialize thread. */
  init_thread (t, name, priority);
  
  if(thread_current()->cwd != NULL)
    t->cwd = dir_reopen(thread_current()->cwd);
 
  
  t->tid = allocate_tid ();
  t->parent = thread_tid();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();
  
  /* Stack frame for start_exec_process */
  if_ = alloc_frame(t, sizeof *if_);
  
  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;
  
  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

#ifdef USERPROG
  t -> fd_table = (struct list *)malloc (sizeof (struct list));
  list_init (t -> fd_table);
  /* Assuming 128 to be a maximum */
  t -> fd_entry =bitmap_create (500);
  /* setting true for initial console*/
  t -> fd_std_in = true;
  t -> fd_std_err = true;
  t -> fd_std_out = true;
  bitmap_set(t -> fd_entry, 0, true); 
  bitmap_set(t -> fd_entry, 1, true); 
  bitmap_set(t -> fd_entry, 2, true); 
#endif

  intr_set_level (old_level);

  return t;
}

tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  //printf("inside thread cretae\n");
	struct thread *t;
	t = thread_create_blocked (name, priority, function, aux);
  
  if (t == NULL)
  	return TID_ERROR;
	else
  	/* Add to run queue. */
  	thread_unblock (t);
  return t->tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

struct thread *
get_thread (tid_t tid)
{
  ASSERT(intr_get_level() == INTR_OFF);
 //TODO: remove the interrut disable 
  tid_t pid = tid;
  struct list_elem *e;
  struct thread *t = NULL;
  //int old_level = intr_disable();
  for (e = list_begin(&all_list); e != list_tail(&all_list); e = list_next(e))
  {
    t = list_entry(e, struct thread, allelem);
    if (t->tid == pid)
      break;
    else
      t = NULL;
  }
  //intr_set_level(old_level);
  return t;
}

struct exit_thread *
get_exit_thread (tid_t tid)
{
  ASSERT(intr_get_level() == INTR_OFF);
  tid_t pid = tid;
  struct list_elem *e;
  struct exit_thread *t = NULL;
  //int old_level = intr_disable();
  for (e = list_begin(&dead_list); e != list_tail(&dead_list); e = list_next(e))
  {
    t = list_entry(e, struct exit_thread, elem);
    if (t->tid == pid)
      break;
    else
      t = NULL;
  }
  //intr_set_level(old_level);
  return t;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (int exit_code) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif
  
//  lock_acquire(&file_lock);
  dir_close(thread_current()->cwd);
 
  if (thread_current () -> current_executable)
  {
  	file_close(thread_current()->current_executable);
    fd_mem_free();
    bitmap_destroy(thread_current() -> fd_entry);
  }
//  lock_release(&file_lock);
  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it call schedule_tail(). */
  intr_disable ();

  

  
  if (thread_current() -> load_status)
    printf("%s: exit(%d)\n", thread_current ()->name, exit_code);
 
  /* do not create exit structure if current thread is orphan */
  if(get_thread(thread_current() -> parent) != NULL)
  {
    struct exit_thread *exit = (struct exit_thread *) malloc (sizeof (struct exit_thread));
    exit->tid = thread_current ()->tid;
    exit->parent = thread_current ()->parent;
    exit->exit_code = exit_code;
    exit -> load_status = thread_current() -> load_status;
    list_push_front (&dead_list, &exit->elem);
  }
  /* prevent current thread's children from becoming zombie */
  struct list_elem *e;
  for (e = list_begin (&dead_list); e != list_end (&dead_list);
       e = list_next (e))
    {
      struct exit_thread *t = list_entry (e, struct exit_thread, elem);
      if (t->parent == thread_current() -> tid)
      {
        struct list_elem *next = list_remove (e);
        thread_finish (t);
        e = list_prev (next);
      }
    }
  /* Wake up all the threads who have called wait on this thread. */
  thread_wakeup();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

void
thread_finish (struct exit_thread *t)
{
  ASSERT(intr_get_level() == INTR_OFF);
  list_remove (&t->elem);
  free (t);
}

/* Gives up the thread T resources. Make sure that
   t is not in the ready list but in the all threads
   list. Also it cannot be called on the currently 
   executing thread. */
/*void thread_cleanup (struct thread *t)
{
  ASSERT(thread_current() != t);
  int old_level = intr_disable();
  list_remove(&t->allelem);
   list_remove(&t->elem);
  intr_set_level(old_level);
  printf("-------------------\n");
  sup_page_table_destroy(t->sup_pt);
  printf("***********************\n");
  pagedir_destroy(t->pagedir);
  palloc_free_page(t);
}*/

/* Wakes up all the threads sleeping on the current thread.
 * Should be called with interrupts off, preferrably from 
 * thread_exit() */
void
thread_wakeup (void)
{
  ASSERT(intr_get_level() == INTR_OFF);

  struct list_elem *e;
  struct thread *t;
  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e))
  {
    t = list_entry(e, struct thread, allelem);
    if(t->wait_on == thread_tid())
    {
      t->wait_on = -1;
      thread_unblock(t);
    }
  }
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_push_back (&ready_list, &cur->elem);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);
  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit (-1);       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  t->wait_on = -1;
  t -> load_status = false;
  t -> wait_on_exec = false;
  list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
schedule_tail (struct thread *prev) 
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);
  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until schedule_tail() has
   completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  schedule_tail (prev); 
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;
  
  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/* finds the next available fd for allocation */
unsigned fd_next_available ()
{
  return bitmap_scan (thread_current() -> fd_entry, 0,
              1, false);
}

/*
 * Add a file F to our file descriptor table,
 * and return corresponding fd
 */ 
unsigned add_file( struct file *f)
{
   struct fd_table_element *fd_elem = (struct fd_table_element *)malloc(sizeof (struct fd_table_element));
   fd_elem -> file_name = f;
   fd_elem -> fd = fd_next_available();
   bitmap_mark (thread_current() -> fd_entry, fd_elem -> fd);
   list_push_back (thread_current() -> fd_table, &fd_elem -> file_elem);
   return fd_elem -> fd;
}

/*
 * Find a file with gived fd in current thread
 * Currently not handling console
 */
struct fd_table_element * find_file (unsigned fd)
{
  struct list_elem *iterator;
  struct list *fd_table = thread_current ()->fd_table;
  struct fd_table_element *result = NULL;
  struct fd_table_element *temp;
  for (iterator = list_begin (fd_table); iterator != list_end (fd_table); iterator = list_next (iterator))
  {
    temp = list_entry (iterator, struct fd_table_element, file_elem);
    if (temp -> fd == fd)
    {
      result = temp;
      break;
    }
  }
  return result;
}

/*
 * Remove the entry from the fd_entry
 */
void entry_remove (unsigned fd)
{
  /* std_in was directed to conslole initially, if this fag is still set then
   * unset the flag and now fd 0 will behave normally
   */
  if (fd == 0 && thread_current() -> fd_std_in)
    thread_current() -> fd_std_in = false;

  /* similarly */
  if (fd == 1 && thread_current() -> fd_std_out)
    thread_current() -> fd_std_out = false;
  if (fd == 2 && thread_current() -> fd_std_err)
    thread_current() -> fd_std_err = false;

  bitmap_reset (thread_current() -> fd_entry, fd);
}

/* Call this function with interupts disable */
void fd_mem_free (void)
{
  struct list_elem *iterator;
  struct list *fd_table = thread_current ()->fd_table;
  struct fd_table_element *temp;
  for (iterator = list_begin (fd_table); iterator != list_end (fd_table); )
  {
    temp = list_entry (iterator, struct fd_table_element, file_elem);
    iterator = list_next (iterator);
    file_close (temp -> file_name);
    free (temp);
  }
  free (fd_table);
}

void thread_filesys_init (void)
{
  // create the filesys thread which will flush the buffer cache periodically
  thread_create ("Filesys", PRI_DEFAULT, filesys_thread, (void *)NULL);
}

// Function to flush the buffer cache table periodically
void filesys_thread (void *arg UNUSED)
{
  uint64_t sleep_time = 1000;

  while (true)
  {
    timer_sleep (sleep_time);
    // Flush the buffer cache tablehrea
    flush_buffer_cache ();
    //~ printf ("flushed the bcache table \n");
  }
}

// Function for filesys_readahead thread. This function maintains the list of
// readahead requests and fetches them on context switch
void filesys_readahead_thread (void *arg UNUSED)
{
  // initializations
  lock_init (&readahead_lock);
  list_init (&readahead_list);
  cond_init (&readahead_condition);

  while (true)
  {
    // Lock should be acquired before calling cond_wait, thus acquiring lock
    lock_acquire (&readahead_lock);

    // Condition is to wait until anyone puts something on the list of requested
    // readaheads. If someone puts something, it'll signal us and we'll wake up.
    // After waking up, we'll fulfil the request.
    // NOTE: This is *needed* to be a while loop, it cannot be an if condition,
    // i.e. if list is empty, then wait, because sending and receiving a signal
    // are not atomic operations. Thus we need to recheck the condition.
    while (list_empty (&readahead_list))
      cond_wait (&readahead_condition, &readahead_lock);

    // Pop the first request from the front and try to fulfill it
    struct readahead_entry *rentry = 
      list_entry (list_pop_front (&readahead_list), struct readahead_entry, elem);
    lock_release (&readahead_lock);

    fulfill_readahead (rentry -> bsector);

    // We are done with rentry, thus freeing it.
    free (rentry);
  }

}
