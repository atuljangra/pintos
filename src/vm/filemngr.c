#include "vm/vm.h"

static struct hash * fmngr;

unsigned hash_func (const struct hash_elem *, void * UNUSED);
bool less_func (const struct hash_elem *,
                      const struct hash_elem *,
                      void * UNUSED);
struct filemngr_entry * hash_lookup (const void *);

/*
 * Computes and returns the hash value for hash element E, given
 * auxiliary data AUX. *
 */
unsigned hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  const struct filemngr_entry *p = hash_entry (e, struct filemngr_entry,
                                                  hash_elem);
  return hash_bytes ((&p -> filename + (char )&p -> offset),
         sizeof (p -> filename + (char )p -> offset));
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
  const struct filemngr_entry *a_entry = hash_entry (a, struct filemngr_entry,
                                                  hash_elem);
  const struct filemngr_entry *b_entry = hash_entry (b, struct filemngr_entry,
                                                  hash_elem);
  return (&a_entry -> filename + (char )&a_entry -> offset) < (&b_entry -> filename + (char )&b_entry -> offset);
}
/*
 * Initialize the sup_page table entry of the current thread
 */
void filemngr_init (void)
{
  fmngr = (struct hash *)malloc (sizeof (struct hash));
  hash_init (fmngr, hash_func, less_func, NULL);
}
