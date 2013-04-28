#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/kernel/list.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/palloc.h"

struct lock frame_list_lock;

enum frame_location {
  PHYS_MEM,
  SWAP,
  FILE_SYS,
  NOLOC,
};

struct frame
{
  bool    mmapped;          /* Whether the frame is a mmap frame. */
  bool    untracked;        /* Whether the frame is in the frame list. */
  bool    in_swap;          /* Whether the frame is in the swap. */
  void    *addr;            /* Physical address of the frame/sector */
  struct  list_elem elem;   /* To maintain a list of frames. */ 
  struct  list user_list;   /* List of users using this frame. */
  bool    writable;         /* Whether the frame is writable. */
  struct  file *file;        /* If mapped, then to which file. */
  off_t   ofs;              /* Mapped to which offset in file. */
  int     magic;
  int     read_bytes;
  struct  lock lk;          /* Lock to synchronize accesses to the frame. */ 
};

struct user
{
  tid_t   tid;              /* The process who installed the frame. */
  void    *vaddr;           /* The virtual address where the frame is installed. */
  struct  list_elem elem;   /* List elem to create a list. */
};

static inline enum frame_location
get_frame_loc (struct frame *frame)
{
  enum frame_location loc;
  if (!(frame -> in_swap))
    loc = PHYS_MEM;
  else if (frame -> mmapped || !(frame -> writable))
    loc = FILE_SYS;
  else
    loc = SWAP;
  return loc;
}

void frame_init (void);
struct frame * frame_create (void *addr);
struct frame * frame_alloc (enum palloc_flags );
struct frame * frame_alloc_lockless (enum palloc_flags );
bool frame_dealloc (struct frame *frame, void *vaddr);
void frame_track (struct frame *, void *);
void frame_untrack (struct frame * frame, void *vaddr);
bool frame_in (struct frame *f);
bool frame_is_dirty (struct frame *frame);
#endif
