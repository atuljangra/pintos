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
int evict_bcache (void);


static int miss = 0;
static int hit= 0;
static int requested = 0;
void print_my_ass ()
{
 printf (" req : %d, hit: %d, miss%d \n\n", requested, hit, miss);
}
void init_bcache ()
{
  int i, j;
  for (i = 0; i < BUFFER_CACHE_SIZE/sector_per_page; i++)
  {
    void *kpage = palloc_get_page (PAL_ZERO);
    if (!kpage)
      PANIC ("get lost now, no page\n");
    for (j = 0; j < sector_per_page; j++)
    {
      struct bcache_entry *temp = (struct bcache_entry *) malloc (sizeof (struct bcache_entry));

      temp -> bsector = -1;
      // starting is not kpage, it it *in* the kpage but with some offset.
      // No need to be page aligned.
      temp -> kaddr = kpage + BLOCK_SECTOR_SIZE * j;
      temp -> dirty = 0;
      temp -> accessed = 0;
      temp -> read = 0;
      temp -> write = 0;
      temp -> valid = false;
      lock_init (&temp -> lock); 
      bcache[i * sector_per_page+j] = temp;
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
  //lock_acquire (&bcache_lock);
  int i = 0;
  requested ++;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    // printf ("finding sector, iteration : %d buffer cache : %x \n", i, bcache[i]);
    if (bcache [i] == NULL )
      PANIC ("NULL CACHE \n");
    if (bcache[i] -> bsector == blockid)
    {
      if (flag == FLAG_READ)
        bcache[i] -> read++;
      if (flag == FLAG_WRITE)
        bcache[i] -> write++;

      //lock_release (&bcache_lock);
      hit ++;
      return i;
    }
  }

  miss++;
  //lock_release (&bcache_lock);
  return -1;
}

// Adds a block to buffer cache
size_t add_bcache (block_sector_t blockid, int flag)
{

  //lock_acquire (&bcache_lock);
  // Find a free entry in the bitmap table
  size_t free_entry = bitmap_scan (bcache_table, 0, 1, false);


  if (free_entry != BITMAP_ERROR)
  {
    bcache[free_entry] -> dirty = false;
    bcache[free_entry] -> accessed = false;
    bcache[free_entry] -> bsector = blockid;
    bcache[free_entry] -> valid = false;
    // Mark the corresponding entry in bitmap table
    bitmap_set (bcache_table, free_entry, true);
    if (flag == FLAG_READ)
      bcache[free_entry] -> read ++;
    if (flag == FLAG_WRITE)
      bcache[free_entry] -> write ++;
    lock_release (&bcache_lock);
    // read from the disk
    block_read (fs_device, blockid, bcache[free_entry] -> kaddr);
    // mark the buffer as valid
    lock_acquire (&bcache[free_entry] -> lock);
    bcache[free_entry] -> valid = true;
    lock_release (&bcache[free_entry] -> lock);
    return free_entry;
  }
  
  // If there is no such entry then we need to evict
  if (free_entry == BITMAP_ERROR)
  {
    int evicted = evict_bcache ();
    
    block_write (fs_device, bcache[evicted] -> bsector, bcache[evicted] -> kaddr);

    bcache[evicted] -> dirty = false;
    bcache[evicted] -> accessed = false;
    bcache[evicted] -> bsector = blockid;
    bcache[evicted] -> valid = false;
    
    // Mark the corresponding entry in bitmap table
    if (flag == FLAG_READ)
      bcache[evicted] -> read ++;
    if (flag == FLAG_WRITE)
      bcache[evicted] -> write ++;
    lock_release (&bcache_lock);

    // read from the disk    
    block_read (fs_device, blockid, bcache[evicted] -> kaddr);

    // mark the buffer as valid
    lock_acquire (&bcache[evicted] -> lock);
    bcache[evicted] -> valid = true;
    lock_release (&bcache[evicted] -> lock);
    
    return evicted;
  }

  return -1;
}

// reads from a block, which is in cache, and stores it in buffer.
void read_bcache (block_sector_t blockid, void *buffer, off_t offset, int size)
{
  // sanitychecks
  ASSERT (offset < BLOCK_SECTOR_SIZE);

  lock_acquire(&bcache_lock);
  // flag is 0 as it is a read access
  int entry = find_sector (blockid, FLAG_READ);

  // If no entry is there, then add the cache and increase the reader.
  if (entry == -1)
  {
    entry = add_bcache (blockid, FLAG_READ);
  }
  else
  {  
    lock_release(&bcache_lock);
    while (bcache[entry] -> valid == false)
    {
      lock_acquire (&bcache[entry] -> lock);
      lock_release (&bcache[entry] -> lock);
    }
  }

  // Copying contents into the requested buffer
  memcpy (buffer, (bcache[entry] -> kaddr + offset), size);
  lock_acquire(&bcache[entry] -> lock);
  bcache[entry] -> accessed = true;
  bcache[entry] -> read--;
  lock_release(&bcache[entry] -> lock);

}

// Write to the cache from buffer at offset OFFSET and of size SIZE
void write_bcache (block_sector_t blockid, void *buffer, int offset, int size)
{
  //sanity checks
  ASSERT (offset < BLOCK_SECTOR_SIZE);

  lock_acquire(&bcache_lock);
  int entry = find_sector (blockid, FLAG_WRITE);

  // If no entry is there, then add the cache and increase the reader.
  if (entry == -1)
  {
    entry = add_bcache (blockid, FLAG_WRITE);
  }
  else
  {

    lock_release(&bcache_lock);
    while (bcache[entry] -> valid == false)
    {
      lock_acquire (&bcache[entry] -> lock);
      lock_release (&bcache[entry] -> lock);
    }
  }
  
  // Copying contents into the requested buffer
  memcpy ((bcache[entry] -> kaddr + offset), buffer, size);

  lock_acquire(&bcache[entry] -> lock);
  bcache[entry] -> accessed = true;
  bcache[entry] -> dirty = true;

  bcache[entry] -> write--;
  lock_release(&bcache[entry] -> lock);

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
  bcache[index]->dirty = false;
  bcache[index] -> write --;
}

// Evict cache to make place for another entry into the cache
int evict_bcache ()
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
          return evicted;
        }
        else
          bcache[i] -> accessed = false;
      }
  }
  // never should have come here
  return -1;
}

// Function to flush the entire buffer cache to disk. 
// This is called periodically by a kernel thread to ensure consistency of data.
void flush_buffer_cache ()
{
  lock_acquire (&bcache_lock);

  int i;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
    writeback (i);

  lock_release (&bcache_lock);
}

// fulfills the readahead requests that was made while adding an entry to bcache
// and is being fulfilled by the read_ahead thread.
void fulfill_readahead (block_sector_t blockid)
{
  //If not already in cache, add it into the cache. 
  lock_acquire(&bcache_lock);
  if (find_sector (blockid, FLAG_NONE) == -1)
    add_bcache (blockid, FLAG_NONE);
  else
   lock_release(&bcache_lock);
  // Otherwise we are good, so nothing to do at all
}

// This is the function to request readahead 
void request_readahead (block_sector_t next_blockid)
{
  lock_acquire (&readahead_lock);

  struct readahead_entry *rentry = 
    (struct readahead_entry *) malloc (sizeof (struct readahead_entry));
  rentry -> bsector = next_blockid;

  // Add the requested entry to the global list of requests
  list_push_back (&readahead_list, &rentry -> elem);

  // signal that the readahead thread can wake 
  cond_signal (&readahead_condition, &readahead_lock);

  // release the readahead lock
  lock_release (&readahead_lock);
}

