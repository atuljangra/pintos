#ifndef THREADS_THREAD_H
//487
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"
#include "lib/kernel/hash.h"
#include "filesys/file.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)

/* Map region identifier. */
typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
#define STACK_MAX_PAGES 20480            /* Maximum number of stack pages */
/* Debugging mode*/
// #define DEBUG
/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    tid_t parent;                       /* Parent's identifier */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */
    
    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */
    bool wait_on_exec;                  /* Is the thread waiting for execed child to load. */
    bool load_status;                  /* Whether the executable was successfully loaded. */
    tid_t wait_on;                      /* Thread to sleep on */
#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory.*/
    struct list * fd_table;             /* fd table list pointer*/
    struct bitmap *fd_entry;            /* keep record of used fd*/
    struct file * current_executable;   /* currently executing file*/
    struct hash * sup_pt;               /* Supplemental page table */
    struct lock pt_lock;                /* lock for sup_page table */
    struct list * mapping;              /* List of mmaped files */

    bool fd_std_in;                     /* boolean to keep */
    bool fd_std_out;                    /* track of console*/
    bool fd_std_err;                    /* IO */
    void *user_stack_bottom;            /* user stack */
    void *esp;                           /* esp of this thread */
#endif
    int exit_code;                      /* Exit code. */
    uint32_t pages[383];
    /* Owned by thread.c. */
    
    struct dir *cwd;                    /* current working directory */
    unsigned magic;                     /* Detects stack overflow. */
  };
  
struct exit_thread
{
  tid_t tid;
  tid_t parent;
  bool load_status;        /* Whether the executable was successfully laoded. */
  struct list_elem elem;    /* List element for dead processes list */
  int exit_code;            /* Exit code set by on thread exit */
};

struct fd_table_element
{
  struct file *file_name;
  unsigned fd;
  struct list_elem file_elem;            /* hanger of list */
};

struct sup_page_table_entry
{
  void *vaddr;                          /* virtual address */
  struct frame *frame;                  /* frame correspoding to this entry */
  struct hash_elem hash_elem;           /* hash element hanger for pte */
  bool file_mapped;                     /* is this a mmap file mapping? */
  struct file * file;                   /* file if it's an executable mapping */
  off_t offset;
  uint32_t page_read_bytes;             /* page_read_byes of the mmaped file */
  bool writable;
  bool shared;                          /* Whether to have a private copy or a shared copy. */
};

struct mapped_entry
{
  mapid_t id;                             /* mapid */
  struct file* file;                      /* the mmaped file pointer */
  struct list_elem list_elem;             /* hanger for list */
  void *vaddr;                            /* virtual address where mapped */
};

/* Functions to modify fd_table */
unsigned add_file (struct file *);
struct fd_table_element * find_file (unsigned);
void remove_file (struct file *);
unsigned fd_next_available (void);
void entry_remove(unsigned fd);

/* Functions for filesys */
void thread_filesys_init (void);
/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);
struct thread *thread_create_blocked (const char *name, int priority, thread_func *function, void *aux);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (int) NO_RETURN;
void thread_finish (struct exit_thread *);
//void thread_cleanup (struct thread *t);
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

struct thread *get_thread (tid_t);
struct exit_thread *get_exit_thread (tid_t);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

#endif /* threads/thread.h */
