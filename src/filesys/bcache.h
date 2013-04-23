#ifndef FILESYS_BCACHE_H
#define FILESYS_BCACHE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "devices/block.h"
#include "threads/synch.h"
#include <stdio.h>
#include "filesys/off_t.h"

#define BUFFER_CACHE_SIZE 64
#define FLAG_READ 0
#define FLAG_WRITE 1
#define FLAG_NONE -1

struct bcache_entry {
  block_sector_t bsector;       /* Block Sector */
  void *kaddr;                  /* Kernel page corresponding to this entry */
  bool dirty;                   /* Is this entry dirty */
  bool accessed;                /* accessed bit corresponding to this entry */
  int read;                     /* number of processos reading on this thread*/
  int write;                    /* number of processes writing on this thread */
};

/* Structure for maintaining all the readahead requests that needs
 * to be handled by readahead thread
 */
struct readahead_entry {
  block_sector_t bsector;       /* Block sector */
  struct list_elem elem;         /* list element hanger */
};

/* For read ahead thread */
struct lock readahead_lock;
struct list readahead_list;
struct condition readahead_condition;


void init_bcache (void);
size_t add_bcache (block_sector_t blockid);
int find_sector (block_sector_t blockid, int flag);
void read_bcache (block_sector_t blockid, void *buffer, off_t offset, int size);
void write_bcache (block_sector_t blockid, void *buffer, int offset, int size);
void flush_buffer_cache (void);

void fulfill_readahead (block_sector_t blockid);
#endif
