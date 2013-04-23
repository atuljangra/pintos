#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include "threads/vaddr.h"
#include "lib/kernel/bitmap.h"
#include "threads/palloc.h"
#include "filesys/filesys.h"
#include "lib/string.h"
#include "threads/thread.h"
#include "filesys/bcache.h"

struct bcache_entry * bcache[BUFFER_CACHE_SIZE];
int sector_per_page = (PGSIZE/BLOCK_SECTOR_SIZE);
struct bitmap * bcache_table;
struct lock bcache_lock;

void writeback (int);
void evict_bcache (void);

void init_bcache ()
{
  int i, j;
  for (i = 0; i < BUFFER_CACHE_SIZE/sector_per_page; i++)
  {
    void *kpage = palloc_get_page (PAL_ZERO);
    struct bcache_entry *temp;
    for (j = 0; j < sector_per_page; j++)
    {
      temp = (struct bcache_entry *) malloc (sizeof (struct bcache_entry));

      temp -> bsector = -1;
      // starting is not kpage, it it *in* the kpage but with some offset.
      // No need to be page aligned.
      temp -> kaddr = kpage + BLOCK_SECTOR_SIZE * j;
      temp -> dirty = 0;
      temp -> accessed = 0;
      temp -> read = 0;
      temp -> write = 0;
      bcache[i * (sector_per_page+j)] = temp;
    }
  }

  //initialize the global lock for buffer cache
  lock_init (&bcache_lock);

  // create the bitmap
  bcache_table = bitmap_create (BUFFER_CACHE_SIZE);
}

// Finds an entry corresponding to blockid
// flag: either 0(read) or 1(write), increase the corresponding entry of block.
// to avoid race conditions, as done in xv6
int find_sector (block_sector_t blockid, int flag)
{
  while (!lock_try_acquire (&bcache_lock))
    thread_yield();
  int i = 0;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (bcache[i] -> bsector == blockid)
    {
      if (flag == 0)
        bcache[i] -> read++;
      if (flag == 1)
        bcache[i] -> write++;

      lock_release (&bcache_lock);
      return i;
    }
  }

  lock_release (&bcache_lock);
  return -1;
}

// Adds a block to buffer cache
size_t add_bcache (block_sector_t blockid)
{
  while (!lock_try_acquire (&bcache_lock))
    thread_yield();

  // Find a free entry in the bitmap table
  size_t free_entry = bitmap_scan (bcache_table, 0, 1, false);

  // If there is no such entry then we need to evict
  if (free_entry == BITMAP_ERROR)
  {
    PANIC("eviction not implemented \n");
  }

  ASSERT (free_entry != BITMAP_ERROR);
  printf ("Adding bcache block %d \n", blockid);
  block_read (fs_device, blockid, bcache[free_entry] -> kaddr);
  bcache[free_entry] -> dirty = false;
  bcache[free_entry] -> accessed = false;
  bcache[free_entry] -> bsector = blockid;

  // Mark the corresponding entry in bitmap table
  bitmap_set (bcache_table, free_entry, true);

  lock_release (&bcache_lock);
  return free_entry;
}

// reads from a block, which is in cache, and stores it in buffer.
void read_bcache (block_sector_t blockid, void *buffer, off_t offset, int size)
{
  // sanitychecks
  ASSERT (offset < BLOCK_SECTOR_SIZE);
  printf ("Reading bcache at %d, offset %d, size %d \n", blockid, offset, size);

  // flag is 0 as it is a read access
  int entry = find_sector (blockid, 0);

  // If no entry is there, then add the cache and increase the reader.
  if (entry == -1)
  {
    entry = add_bcache (blockid);
    bcache[entry] -> read++;
  }

  // Copying contents into the requested buffer
  memcpy (buffer, (bcache[entry] -> kaddr + offset), size);

  bcache[entry] -> accessed = true;
  bcache[entry] -> read--;

}

// Write to the cache from buffer at offset OFFSET and of size SIZE
void write_bcache (block_sector_t blockid, void *buffer, int offset, int size)
{
  //sanity checks
  ASSERT (offset < BLOCK_SECTOR_SIZE);
  printf ("Writing bcache at %d, offset %d, size %d buffer %s \n", blockid, offset, size, buffer);

  // flag is 1 as it is a write access
  int entry = find_sector (blockid, 1);

  // If no entry is there, then add the cache and increase the reader.
  if (entry == -1)
  {
    entry = add_bcache (blockid);
    // Register as writer process
    bcache[entry] -> write++;
  }

  // Copying contents into the requested buffer
  memcpy ((bcache[entry] -> kaddr + offset), buffer, size);

  bcache[entry] -> accessed = true;
  bcache[entry] -> dirty = true;

  // End write
  bcache[entry] -> write--;

}

// Writeback entry corresponding to index INDEX to disk. 
void writeback (int index)
{
  // Sanitycheck
  if (bitmap_test (bcache_table, index) == false)
    return;

  // Important to register this as a writing process, otherwise, accessing this,
  // when we are evicting it out, can create race conditions.
  bcache[index] -> write ++;

  // Write back only if it is set as dirty
  if (bcache[index] -> dirty == true)
    block_write (fs_device, bcache[index] -> bsector, bcache[index] -> kaddr);

  //End write
  bcache[index] -> write ++;
}

// Evict cache to make place for another entry into the cache
// TODO: Currently using second change algorithm, as clock was creating problem
// TODO: Change this to use clock
void evict_bcache ()
{
  int i;
  int evicted = -1;
  while (evicted == -1)
  {
    for (i = 0; i < BUFFER_CACHE_SIZE; i++)
      // is the entry completely free
      if (bcache[i] -> write == 0 && bcache[i] -> read == 0)
      {
        if (bcache[i] -> accessed == false)
        {
          evicted = i;
          writeback (evicted);
          bitmap_set (bcache_table, evicted, false);
          return;
        }
        else
          bcache[i] -> accessed = false;
      }
  }
}
