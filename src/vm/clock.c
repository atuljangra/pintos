#include "lib/kernel/list.h"
#include "threads/thread.h"
#include "vm/frame.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"
#include "vm/clock.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/interrupt.h"

/* Current position of the clock hand. */
struct list_elem * cur_frame_elem;
static inline enum frame_location evict_to (struct frame *frame);

/*
 * Initialize the eviction
 */
void
evict_init ()
{
  cur_frame_elem = NULL;
}

/* Evicts a frame from the physical memory to the swap space.
   The evicted frame is removed from the frame list. This function
   returns the frame that succeeds the evicted frame. */
struct frame *
evict_frame (struct list * frame_list)
{
  if (!has_swap())
    return NULL;
  ASSERT (!list_empty (frame_list));
  struct frame * frame = NULL;
  
  /* Traverse the frame list in circle until a frame is found for eviction. */
  for (cur_frame_elem = ((cur_frame_elem == NULL || cur_frame_elem == list_end(frame_list)) ? list_front (frame_list) : cur_frame_elem) ;
      ; cur_frame_elem = ((cur_frame_elem == list_back (frame_list))? list_front (frame_list) : list_next (cur_frame_elem)) )
  {
    frame = list_entry (cur_frame_elem, struct frame, elem);
    ASSERT(frame -> magic == 0x00345678);
    lock_acquire (&frame -> lk);
    if (list_empty (&frame -> user_list))
    {
      cur_frame_elem = list_next(cur_frame_elem);
      return frame;
    }
    struct list_elem * e;
    bool accessed = false;
    bool dirty = false;
    /* There must be some user tracking this frame. Otherwise this frame 
       had already been removed from the frame list. */ 

     ASSERT (!list_empty(&frame->user_list));
    int old_level = intr_disable(); 
    /* Set the accessed bit to 0 for all the pages mapping this frame. */
    for (e = list_begin (&frame -> user_list); e != list_end (&frame -> user_list); e = list_next(e))
    {
      struct user *user = list_entry (e, struct user, elem);
      struct thread * t = get_thread (user -> tid);
      if (t != NULL)
      {
        accessed |= pagedir_is_accessed (t -> pagedir, user -> vaddr);
        dirty |= pagedir_is_dirty (t -> pagedir, user -> vaddr);
      }
      pagedir_set_accessed (t -> pagedir, user -> vaddr, false);
    }
    intr_set_level (old_level);
    /* The frame was not recently accessed. */
    if (!accessed)
    {
      int old_level = intr_disable();
      /* Mark the pages using FRAME as not present, anymore. */
      for (e = list_begin (&frame -> user_list); e != list_end (&frame -> user_list); e = list_next(e))
      {
        struct user *user = list_entry (e, struct user, elem);
        struct thread * t = get_thread (user -> tid);
        if (t != NULL)
        {
          /* Mark the pages using this frame as not present. */
          pagedir_clear_page (t -> pagedir, user -> vaddr);
        }
      }
      intr_set_level (old_level);
      /* Move the clock's hand to the next frame. */
      struct list_elem *temp_hand = list_next (cur_frame_elem);
      /* Insert a new frame in place of evicted frame. */
      struct frame *new_frame = frame_create (frame -> addr);
      list_remove (&frame -> elem); 
      list_insert (temp_hand, &new_frame -> elem);
      /* Evict the frame from the frame list. */

      bool swapped = false;
      enum frame_location evict_loc = evict_to (frame);      
      if (evict_loc == FILE_SYS)
      {
        uint32_t bytes = file_write_at (frame -> file, ptov ((uintptr_t) frame -> addr), PGSIZE, frame -> ofs); 
        if (bytes != 0)
        {
          swapped = true;
          frame -> in_swap = true;
        }
        /* Get the file name and the offset using the supplemental 
           page table of one of the threads tracking this frame. */ 
      } 
      /* The page was read-only or it was not yet dirty therefore just set it evicted. */
      else if (evict_loc == NOLOC)
      {
        swapped = true;
        frame -> in_swap = true;
      }
      /* Evict the frame to the swap. */
      /* Updates the boolean value in_swap and addr with the address of the frame in the swap. */
      else
        swapped = swap_out (frame);
      
      /* Swap was unsuccessful. Return the unmodified list. */
      if (!swapped)
      {
        list_insert (list_remove (&new_frame -> elem), &frame -> elem);
        free (new_frame);
        lock_release (&frame -> lk);
        /* If swapping was not successful, due to eviction of mmap page
           try again else return error. */
          if (frame -> mmapped)
          continue;
        else
          return NULL; 
      }
        cur_frame_elem = temp_hand;
      /* Swap was successful. Return the frame next to the evicted 
         frame in the original fram list. */
      lock_release (&frame -> lk);
      return new_frame;
    }
    lock_release (&frame -> lk);
  }
}

/*
 * Frequently used function thus inlined
 */
static inline enum frame_location
evict_to (struct frame *frame)
{
  enum frame_location loc;
  struct thread *t = thread_current();
  void *upage = ptov ((uint32_t)(frame -> addr));
  bool dirty = pagedir_is_dirty ((uint32_t *) (t -> pagedir), (const void *) upage);
  if (frame -> mmapped && dirty)
  {
    loc = FILESYS;
  }
  else if ((frame -> mmapped && !dirty) || (!frame -> mmapped && !frame -> writable))
  {
    loc = NOLOC;
  }
  else
  {
    loc = SWAP;
  }
  return loc;
}


