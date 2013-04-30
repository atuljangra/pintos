/* This file is for maintaining the swap table */
#include "vm/swap.h"
#include "devices/block.h"
#include "lib/kernel/bitmap.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/thread.h"

struct block *swap;
struct bitmap *swap_pool;
static struct lock bitmap_lock;

/* Should be called only after file system has been initialized. */
void
swap_init ()
{

  swap = block_get_role (BLOCK_SWAP);
  if (swap != NULL)
  {
    swap_pool = bitmap_create ((block_size (swap) * BLOCK_SECTOR_SIZE) / PGSIZE);
    if (!swap_pool)
      swap = NULL;
    lock_init (&bitmap_lock);
  }
}

/* Moves the frame to the swap space updating its entries
   to reflect the same. Returns true if swapping was successful,
   false otherwise. Sould be called with lock on frame list acquired.*/
bool
swap_out (struct frame * frame)
{
  //~ printf ("in swap_out \n");
  bool swapped = false;
  int addr = 0;
  if (has_swap())
  {
    lock_acquire (&bitmap_lock);
    uint32_t pg_idx = bitmap_scan_and_flip (swap_pool, 0, 1, false);
    lock_release (&bitmap_lock);
    if (pg_idx != BITMAP_ERROR)
    {
      int block_idx = pg_idx * (PGSIZE / BLOCK_SECTOR_SIZE);
      int i = 0;
      for (i = 0; i < (PGSIZE/BLOCK_SECTOR_SIZE); i++)
      {
        //~ printf ("writing to block i: %d  %d %d \n", i, block_idx + i, ptov ((int)frame -> addr) + i * BLOCK_SECTOR_SIZE);
        addr = ptov ((int)frame -> addr) + i * BLOCK_SECTOR_SIZE;
        block_write (swap, block_idx  + i, addr);
      }
      frame -> addr = (void *)pg_idx;
      frame -> in_swap = true;
      swapped = true;
    }
  }
  //~ printf ("out swap_out \n\n");

  return swapped;
}

/* Moves the FROM frame which is present is swap space to
   TO frame which is present in physical memory. Should be called 
   with the lock on frame list acquired.*/
void
swap_in (struct frame *from, struct frame *to)
{
  ASSERT (swap != NULL);
  ASSERT (from -> in_swap == true);
  ASSERT (to -> in_swap == false);
  ASSERT (bitmap_test (swap_pool, (size_t) from -> addr));
  ASSERT (pg_ofs (to -> addr) == 0);
  int i = 0;
  block_sector_t block_idx = (uint32_t)from -> addr * (PGSIZE / BLOCK_SECTOR_SIZE);
  for (i = 0; i < (PGSIZE / BLOCK_SECTOR_SIZE); i++)
  {
    block_read (swap, block_idx + i, (void *) ((void *)ptov((uintptr_t)to -> addr) + (i * BLOCK_SECTOR_SIZE)));
  }
  swap_free (from -> addr);
  from -> in_swap = false;
  from -> addr = to -> addr;
  ASSERT (!list_empty (&from -> user_list));
}

void
swap_free (void *addr)
{
  lock_acquire (&bitmap_lock);
  ASSERT (bitmap_test (swap_pool, (size_t) addr));
  bitmap_reset (swap_pool, (size_t) addr);
  lock_release (&bitmap_lock);
}

bool
has_swap ()
{
 return swap != NULL;
}
