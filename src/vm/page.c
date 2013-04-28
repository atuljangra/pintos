#include "vm/vm.h"
#include "threads/palloc.h"
#include "vm/frame.h"
#include "vm/sframe.h"
#include "lib/string.h"

unsigned sup_hash_func (const struct hash_elem *, void * UNUSED);
bool sup_less_func (const struct hash_elem *,
                      const struct hash_elem *,
                      void * UNUSED);
struct sup_page_table_entry * hash_lookup (const void *);
static struct frame * sup_page_table_fill_new_frame (struct sup_page_table_entry *sp_entry);


/*
 * Computes and returns the hash value for hash element E, given
 * auxiliary data AUX. *
 */
unsigned sup_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  const struct sup_page_table_entry *p = hash_entry (e, struct sup_page_table_entry,
                                                  hash_elem);
  return hash_bytes (&p -> vaddr, sizeof p -> vaddr);
}
/*
 * Compares the value of two hash elements A and B, given
 * auxiliary data AUX.  Returns true if A is less than B, or
 * false if A is greater than or equal to B.
 */
bool sup_less_func (const struct hash_elem *a,
                      const struct hash_elem *b,
                      void *aux UNUSED)
{
  const struct sup_page_table_entry *a_entry = hash_entry (a, struct sup_page_table_entry,
                                                  hash_elem);
  const struct sup_page_table_entry *b_entry = hash_entry (b, struct sup_page_table_entry,
                                                  hash_elem);
  return a_entry -> vaddr < b_entry -> vaddr;
}
/*
 * Initialize the sup_page table entry of the current thread
 */
void sup_pt_init (void)
{
  struct thread *process;
  process = thread_current();//get_thread (tid);
  process-> sup_pt = (struct hash *)malloc (sizeof (struct hash));
  hash_init (process -> sup_pt, sup_hash_func, sup_less_func, NULL);
  lock_init (&process -> pt_lock);
}

/*
 * Create a entry in sup_page table entry for VADDR
 * DO NOT allocate frame right now, we are looking for lazy loading.
 */
struct sup_page_table_entry *vm_page_create (void *vaddr)
{
  struct sup_page_table_entry *result;
  ASSERT(vaddr);
  struct thread *process;
  process = thread_current();//get_thread (tid);
  while (!lock_try_acquire (&process -> pt_lock))
    thread_yield();
  result = malloc (sizeof (struct sup_page_table_entry));
  if (!result)
    goto done;
  result -> frame = NULL;
  result -> file_mapped = false;
  result -> vaddr = vaddr;
  result -> offset = 0;
  result -> file = NULL;
  result -> page_read_bytes = 0;
  hash_insert (process -> sup_pt, &result -> hash_elem);
  done:
    lock_release (&process -> pt_lock);
    return result;
}

/*
 * Create an entry in sup_page table, and allocate frame, just for testing
 * we need to do the lazy loading afterwards.
 */ 
struct sup_page_table_entry *vm_create_page_and_alloc (void *vaddr)
{
  struct sup_page_table_entry *result;
  ASSERT(vaddr);
  struct thread *process;
  process = thread_current();//get_thread (tid);
  if(find_page_by_vaddr (vaddr) != NULL)
    return NULL;

  result = malloc (sizeof (struct sup_page_table_entry));
  if (!result)
    goto done;
  result -> file_mapped = false;
  result -> vaddr = vaddr;
  result -> writable = true;
  result -> page_read_bytes = 0;
  result -> frame = NULL;
  result -> shared = false;
  result -> file = NULL;
  hash_insert (process -> sup_pt, &result -> hash_elem);
  sup_page_table_load (result);
  done:
    return result;
}

/* Return sup_page_table_entry which resides at VADDR in entry corresponding to
 * thread tid. Useful while allocating pages lazily.
 * No locks needed here.
 */
struct sup_page_table_entry * find_page_by_vaddr (void *vaddr)
{
  
  struct sup_page_table_entry *temp;
  temp = (struct sup_page_table_entry *)malloc (
          sizeof (struct sup_page_table_entry *));
  struct thread *process;
  process = thread_current();//get_thread (tid);
  struct hash_elem * elem;
  temp -> vaddr = (void *) vaddr;
  elem = hash_find (process -> sup_pt, &temp -> hash_elem);
  free (temp);
  if (!elem)
    return NULL;
  
  return hash_entry (elem, struct sup_page_table_entry, hash_elem);
}

bool
sup_page_table_load (struct sup_page_table_entry *spt_entry)
{
  bool success = false;
  ASSERT (spt_entry != NULL);
  if (spt_entry -> frame)
  {
    struct frame *frame = NULL;
    if (spt_entry -> shared)
    {
      /* Check locking in sframe_get. */
      struct sframe *sframe = sframe_get (spt_entry);
      lock_release (&sframe -> lk);
      frame = spt_entry -> file_mapped ? sframe -> mmapped_frame : sframe -> frame;
      ASSERT (frame != NULL);
    }
    else
    {
      frame = (struct frame *) (spt_entry -> frame);
    }
    success = frame_in(frame);
    goto done;//return true;
  }
  else if (!spt_entry -> shared)
  {
    struct frame *frame = sup_page_table_fill_new_frame (spt_entry);
    ASSERT (true && !lock_held_by_current_thread(&frame -> lk));
    if (frame)
    {
      spt_entry -> frame = (void *) frame;
      success = true;
      goto done;
    }
    success = false;
    goto done;
  }
  else
  {
    struct sframe *sframe = sframe_get (spt_entry);
    struct frame **frame = spt_entry -> file_mapped ? &sframe -> mmapped_frame : &sframe -> frame;
    if (*frame == NULL)
    {
      *frame = sup_page_table_fill_new_frame (spt_entry);
      success = (*frame != NULL);
      if (success)
        spt_entry -> frame = (void *) sframe;
      lock_release (&sframe -> lk);
      goto done;
    }
    else
    {
      spt_entry -> frame = (void *) sframe;
      lock_acquire (&(*frame) -> lk);
      frame_track (*frame, spt_entry -> vaddr);
      if (!(*frame) -> in_swap)      
        pagedir_set_page (thread_current() -> pagedir, spt_entry -> vaddr, 
            ptov((uint32_t)(*frame)->addr), spt_entry -> writable); 
      lock_release (&(*frame) -> lk);
      lock_release (&sframe -> lk);
      success = frame_in (*frame);
      goto done;
    }
  }
  done:
  return success;
}

static struct frame *
sup_page_table_fill_new_frame (struct sup_page_table_entry *spt_entry)
{
  struct frame *frame = frame_alloc (PAL_USER);
  if (!frame)
    return NULL;
  uint32_t bytes = 0;
  if (spt_entry -> file != NULL)
  {
    bytes = file_read_at (spt_entry -> file, ptov ((uint32_t)frame -> addr), spt_entry -> page_read_bytes, spt_entry -> offset);
  }
  else
  {
    bytes = 0;
    spt_entry -> page_read_bytes = 0;
  }
  if (bytes != spt_entry -> page_read_bytes)
  {
    frame -> untracked = true;
    lock_release (&frame -> lk);
    return NULL;
  }
  memset (ptov((uint32_t)frame -> addr) + spt_entry -> page_read_bytes, 0, PGSIZE - spt_entry -> page_read_bytes);
  /* Copy the values over from spt to frame */
  /* garbage */
  ASSERT (!frame -> in_swap);
  /* garbage */
  frame -> untracked = false;
  frame -> writable = spt_entry -> writable;
  frame -> mmapped = spt_entry -> file_mapped;
  frame -> file = spt_entry -> file;
  frame -> ofs = spt_entry -> offset;
  frame -> read_bytes = spt_entry -> page_read_bytes;
  bool success = pagedir_set_page (thread_current() -> pagedir, spt_entry -> vaddr, 
      ptov((uint32_t)frame -> addr), spt_entry -> writable);
  if (!success)
  {
    frame -> untracked = true;
    lock_release (&frame -> lk);
    return NULL;
  }
  frame_track (frame, spt_entry -> vaddr);
  lock_release (&frame -> lk);
  return frame;
}

/*
 * Remove an entry from the supplemental page table
 */
static void
vm_page_remove_ (struct hash_elem *e, void *aux UNUSED)
{
    struct sup_page_table_entry *sp_entry = hash_entry (e, struct sup_page_table_entry, hash_elem);
    if (sp_entry -> frame != NULL)
    {
      if (sp_entry -> shared)
      {
        sframe_remove ((struct sframe *) sp_entry -> frame, sp_entry);
      }
      else
      {
        bool killed = frame_dealloc (sp_entry -> frame, sp_entry -> vaddr);
        ASSERT(killed);
      }
    }
    free (sp_entry);
}

void
vm_page_remove (struct sup_page_table_entry *sup_pt)
{
  hash_delete (thread_current () -> sup_pt, &sup_pt -> hash_elem);
  vm_page_remove_ (&sup_pt -> hash_elem, NULL);
}


/*
 * Destroy the supplemental hash table corresponding to the current thread.
 */
void
sup_page_table_destroy (struct hash *sp_pd)
{
  hash_destroy (sp_pd, vm_page_remove_);
}
