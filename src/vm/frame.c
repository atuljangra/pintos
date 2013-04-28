/* This file is for maintaining frame table */
#include "threads/pte.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "vm/clock.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"
#include "lib/string.h"
#include "threads/interrupt.h"

struct list frame_list;  /* List of frames currently in memory. */

/*
 * Initialize our frame table and lock
 */
void frame_init (void)
{
  list_init (&frame_list);
  lock_init (&frame_list_lock); 
  evict_init ();
}

/*
 * Interface function that is called while allocating frame.
 * This function acquires locks and calls frame_alloc_lockless
 */
struct frame *
frame_alloc (enum palloc_flags flags)
{
  //printf("in frame alloc lock is %x\n", &frame_list_lock);
  lock_acquire (&frame_list_lock);
  struct frame *f = frame_alloc_lockless (flags);
  lock_release (&frame_list_lock);
  return f;
}

/* Allocates a frame. Evicts an existing frames if
   necessary. Returns the frame that was allocated. 
   Should be called with FRAME_LOCK acquired. */
struct frame *
frame_alloc_lockless (enum palloc_flags flags)
{
  ASSERT (lock_held_by_current_thread (&frame_list_lock));
  void *page = NULL;
  struct frame *frame = NULL;
  /* Allocating a frame for the user. */ 
  if (flags & PAL_USER)
  {
    page = palloc_get_page (flags);
    /* user pool not yet empty */
    if (page != NULL)
    {
      frame = frame_create ((void *) vtop (page));
      list_push_back (&frame_list, &frame -> elem);
    }
    /* evict a frame if user pool is empty */
    else
    {
      frame = evict_frame (&frame_list);
    }
  }
  /* Allocating a frame for the kernel. */
  else
  {
    /* Should make sure that we never
       run out of pages in kernel pool. */
    flags |= PAL_ASSERT;
    page = palloc_get_page (flags);
    frame = frame_create ((void *) vtop (page));
  }
  return frame;
}

/*
 * Creates a frame and initializes it
 */
struct frame *
frame_create (void *addr)
{
  struct frame *frame = (struct frame *) malloc (sizeof(struct frame));
  frame -> addr = addr;
  frame -> untracked = true;
  frame -> in_swap = false;
  frame -> untracked = true;
  frame -> magic = 0x00345678;
  lock_init (&frame -> lk);
  list_init (&frame -> user_list);
  lock_acquire (&frame -> lk);
  return frame;
}

/*
 * Returns true if the frame was destroyed.
 */
bool
frame_dealloc (struct frame *frame, void *vaddr)
{
  bool last = false;
  lock_acquire (&frame_list_lock);
  /* Should be redundant because if the
     frame is shared by none than no need of lock on frame.
     And if the frame is shared a bigger lock on filesystem must
     had been acquired before calling dealloc. */
  
  lock_acquire (&frame -> lk);
  bool dirty = false;
  if (!frame -> in_swap)
    dirty = frame_is_dirty (frame);
  enum frame_location frame_loc = get_frame_loc (frame);
  frame_untrack (frame, vaddr);
  /* If no one is using this page anymore. */
  if (list_empty (&frame -> user_list))
  {

    /* If the page is in physical memory. */
    if (frame_loc == PHYS_MEM)
    {
      frame -> untracked = true;
      if (frame -> mmapped)
      {
        ASSERT (frame -> file != NULL);
        /* Check if write back was successful. */
        if (dirty)
          file_write_at (frame -> file, ptov((uintptr_t)frame -> addr), frame -> read_bytes, frame -> ofs);
      }
      lock_release (&frame -> lk);
      lock_release (&frame_list_lock);
      return true;
    }
    else
    {
      /* Means the frame is in swap. */
      if (frame_loc == SWAP)
      {
        swap_free (frame -> addr);
        free (frame);
      }
      else
      {
        free (frame);
      }
      frame = NULL;
    }
    last = true;
  }
  else
    lock_release (&frame -> lk);
  lock_release (&frame_list_lock);
  return last;
}

/* Adds current thread to the list of users who have 
   installed FRAME in their page directory. 
   Required for eviction. */
void
frame_track (struct frame * frame_, void *vaddr)
{
  frame_ -> untracked = false;
  ASSERT (lock_held_by_current_thread (&frame_ -> lk));
  ASSERT (frame_ != NULL);
  struct user *user = (struct user *) malloc (sizeof(struct user));
  user -> tid = thread_tid();
  user -> vaddr = vaddr;
  list_push_back (&frame_ -> user_list, &user -> elem);
}

void
frame_untrack (struct frame * frame, void *vaddr)
{                                    
  ASSERT (lock_held_by_current_thread (&frame -> lk));
  ASSERT(frame != NULL);
  tid_t tid = thread_tid();
  struct list_elem *e;
  for (e = list_begin (&frame -> user_list); e != list_end (&frame -> user_list); e = list_next(e))
  {
    struct user *user = list_entry (e, struct user, elem);
    uint32_t *pd = thread_current() -> pagedir;
    if (user -> tid == tid && ( vaddr == NULL || vaddr == user -> vaddr))
    {
      pagedir_clear_page (pd, user -> vaddr);
      struct list_elem *next = list_remove (e);
      free (user);
      e = list_prev (next);
    }
  }
}

/*
 * Brings frame into the physical memory from the appropriate position
 */
bool
frame_in (struct frame *f)
{
  void *swapaddr UNUSED = 0;
  lock_acquire (&frame_list_lock);
  lock_acquire (&f -> lk);
  ASSERT (!list_empty(&f->user_list));
  enum frame_location loc = get_frame_loc (f); 
  if (loc == PHYS_MEM)
  {
    lock_release (&f -> lk);
    lock_release (&frame_list_lock);
    return true;
  }
  else
  {
    struct frame *to = frame_alloc_lockless(PAL_USER);
    if (to == NULL)
    {
      lock_release (&f -> lk);
      lock_release (&frame_list_lock);
      return false;
    }
    else if (loc == SWAP)
    {
      swapaddr = f->addr;
      swap_in (f, to);
    }
    else
    {
      uint32_t bytes = file_read_at (f -> file, ptov ((uint32_t)to -> addr), f -> read_bytes, f -> ofs);
      memset(ptov((uintptr_t)to->addr) + (uint32_t)f -> read_bytes, 0, PGSIZE - f->read_bytes); 
      if (bytes != (uint32_t)f -> read_bytes)
      {
        /* garbage */
        to -> untracked = true;
        lock_release (&to -> lk);    
        lock_release (&f -> lk);
        lock_release (&frame_list_lock);
        return false;
      }
      else
      {
        f -> in_swap = false;
        f -> addr = to -> addr;
      }
    }
    list_insert (&to -> elem, &f -> elem);
    list_remove (&to -> elem);
    free (to);
    int old_level = intr_disable();
    struct list_elem *e;
    for (e = list_begin (&f -> user_list); e != list_end (&f -> user_list); e = list_next (e))
    {
      struct user *user = list_entry (e, struct user, elem);

      struct thread *t = get_thread (user -> tid);
      /* Writable atgument doesn't matter. */
      pagedir_set_page (t -> pagedir, user -> vaddr, ptov((uint32_t)f -> addr), f -> writable);
    }
    intr_set_level (old_level);
    lock_release (&f -> lk);
    lock_release (&frame_list_lock);
    return true;
  }
}

/*
 * Checks if the frame is dirty. This is helpful while deciding when to write to
 * a file and when not.
 */
bool
frame_is_dirty (struct frame *frame)
{
  ASSERT (frame != NULL);
  ASSERT (lock_held_by_current_thread (&frame -> lk));
  ASSERT (!frame -> in_swap);
  int old_level = intr_disable();
  bool dirty = false;    
  struct list_elem *e;
  for (e = list_begin (&frame -> user_list); e != list_end (&frame -> user_list); e = list_next (e))
  {
   struct user *user = list_entry (e, struct user, elem);
   struct thread *t = get_thread (user -> tid);
   dirty |= pagedir_is_dirty (t -> pagedir, user -> vaddr);
   if (dirty)
   break;
   }
  intr_set_level (old_level);
  return dirty;
}
