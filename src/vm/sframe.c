#include "vm/sframe.h"
#include "threads/malloc.h"
#include "filesys/inode.h"
#include "filesys/file.h"
#include "threads/synch.h"

struct hash sframe_table;
struct lock sframe_lock;

unsigned hash_func (const struct hash_elem *, void * UNUSED);
bool less_func (const struct hash_elem *,
                      const struct hash_elem *,
                      void * UNUSED);
struct sframe * hash_lookup (const void *);

/*
 * Computes and returns the hash value for hash element E, given
 * auxiliary data AUX. *
 */
unsigned hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  const struct sframe *p = hash_entry (e, struct sframe,
                                                  hash_elem);
  return hash_bytes (&p -> inode, sizeof(struct inode *) + sizeof(off_t));
}
/*
 * Compares the value of two hash elements A and B, given
 * auxiliary data AUX.  Returns true if A is less than B, or
 * false if A is greater than or equal to B.
 */
bool less_func (const struct hash_elem *a,
                      const struct hash_elem *b,
                      void *aux UNUSED)
{
  const struct sframe *a_entry = hash_entry (a, struct sframe,
                                                  hash_elem);
  const struct sframe *b_entry = hash_entry (b, struct sframe,
                                                  hash_elem);
  bool b4;
  b4 = (a_entry -> inode != b_entry -> inode || a_entry -> offset != b_entry -> offset);
  return b4;
}

/*
 * Initialize the sup_page table entry of the current thread
 */
void 
sframe_init (void)
{
  hash_init (&sframe_table, hash_func, less_func, NULL);
  lock_init (&sframe_lock);
}

/*
 * Lookup the shared frame hash table for entry corresponding to SUP_ENTRY
 */
static struct sframe *
lookup_sframe (struct sup_page_table_entry *spt_entry)
{
struct sframe *sframe = (struct sframe *) malloc (sizeof(struct sframe));
  ASSERT (spt_entry -> file != NULL);
  sframe -> inode = file_get_inode (spt_entry -> file);
  sframe -> offset = spt_entry -> offset;
  struct hash_elem *hash_elem = hash_find (&sframe_table, &sframe -> hash_elem);
  free (sframe);
  return hash_elem == NULL ? NULL : hash_entry (hash_elem, struct sframe, hash_elem);
}

/* 
 * Returns the shared frame with lock acquired. Should be followed by sframe_set. 
 */ 
struct sframe *
sframe_get (struct sup_page_table_entry *spt_entry)
{
  ASSERT (spt_entry -> shared);
  ASSERT (spt_entry -> file_mapped || (!spt_entry -> file_mapped && !spt_entry -> writable));
  ASSERT (!lock_held_by_current_thread (&sframe_lock));//lock_acquire (&sframe_lock);
  lock_acquire (&sframe_lock);
  struct sframe *sframe = NULL;
  sframe = lookup_sframe (spt_entry);
  if (sframe == NULL)
  {
    sframe = sframe_create();
    sframe -> inode = file_get_inode (spt_entry -> file);
    sframe -> offset = spt_entry -> offset;
    if (sframe)
      hash_insert (&sframe_table, &sframe -> hash_elem);
    ASSERT(sframe -> magic == SFRAME_MAGIC);
  }
  else
  {
    lock_acquire (&sframe -> lk);
  }
  lock_release (&sframe_lock);
  return sframe;
}

/*
 * Create a new element in the shared frame hash table
 */
struct sframe *
sframe_create ()
{
  struct sframe *sframe = (struct sframe *) malloc (sizeof (struct sframe));
  if (sframe != NULL)
  {
    sframe -> magic = SFRAME_MAGIC;
    sframe -> frame = NULL;
    sframe -> mmapped_frame = NULL;
    lock_init (&sframe -> lk);
    lock_acquire (&sframe -> lk);
  }
  return sframe;
}

/*
 * Remove entry from the shared frame table
 */
bool
sframe_remove (struct sframe *sframe, struct sup_page_table_entry *spt_entry)
{
  ASSERT (spt_entry -> shared);
  ASSERT (sframe != NULL);
  ASSERT (sframe -> magic == SFRAME_MAGIC);
  bool removed = false;
  lock_acquire (&sframe_lock);
  lock_acquire (&sframe -> lk);
  bool mmapped = spt_entry -> file_mapped;
  struct frame **frame = mmapped ? &sframe -> mmapped_frame : &sframe -> frame;
  bool last = true;
  if (*frame != NULL)
  {
    ASSERT(!lock_held_by_current_thread(&(*frame) -> lk));
    last = frame_dealloc (*frame, spt_entry -> vaddr);
    ASSERT(!lock_held_by_current_thread(&(*frame) -> lk));
  }
  if (last)
    *frame = NULL;
  if (sframe -> mmapped_frame == NULL && sframe -> frame == NULL)
  {
    struct sframe *temp_sframe = sframe_create();
    temp_sframe -> inode = sframe -> inode;
    temp_sframe -> offset = sframe -> offset;
    hash_delete (&sframe_table, &temp_sframe -> hash_elem);
    free (temp_sframe);
    free (sframe);
    removed = true;
  }
  else
    lock_release (&sframe -> lk);
  spt_entry -> frame = NULL;
  lock_release (&sframe_lock); 
  return removed;
}
