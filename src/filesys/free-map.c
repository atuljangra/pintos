#include "filesys/free-map.h"
//2732
#include <bitmap.h>
#include <debug.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

static struct file *free_map_file;   /* Free map file. */
static struct bitmap *free_map;      /* Free map, one bit per sector. */
struct lock free_map_lock;

/* Initializes the free map. */
void
free_map_init (void) 
{
  lock_init(&free_map_lock);
  free_map = bitmap_create (block_size (fs_device));
  if (free_map == NULL)
    PANIC ("bitmap creation failed--file system device is too large");
  bitmap_mark (free_map, FREE_MAP_SECTOR);
  bitmap_mark (free_map, ROOT_DIR_SECTOR);
}

/* Allocates 1 sectors from the free map and stores
   the first into *SECTORP. Also it zeros out the block allocated
   Returns true if successful, false if all sectors were
   available. */
bool
free_map_allocate (size_t cnt, block_sector_t *sectorp)
{
  ASSERT(cnt == 1);
  //void *zeros = malloc(BLOCK_SECTOR_SIZE);
  lock_acquire(&free_map_lock);
  block_sector_t sector = bitmap_scan_and_flip (free_map, 0, cnt, false);
  lock_release(&free_map_lock);
  
  if (sector != BITMAP_ERROR
      && free_map_file != NULL
      && !bitmap_write (free_map, free_map_file))
    {
      lock_acquire(&free_map_lock);
      bitmap_set_multiple (free_map, sector, cnt, false); 
      lock_release(&free_map_lock);
      
      sector = BITMAP_ERROR;
    }
  if (sector != BITMAP_ERROR){
    //memset(zeros,0,BLOCK_SECTOR_SIZE);
    //block_write(fs_device,sector,zeros);
    *sectorp = sector;
  }
  //free(zeros);
  //ASSERT(sector!=BITMAP_ERROR);
  return sector != BITMAP_ERROR;
}

/* Makes CNT sectors starting at SECTOR available for use. */
void
free_map_release (block_sector_t sector, size_t cnt)
{
  lock_acquire(&free_map_lock);
  ASSERT (bitmap_all (free_map, sector, cnt));
  bitmap_set_multiple (free_map, sector, cnt, false);
  lock_release(&free_map_lock);
  
  bitmap_write (free_map, free_map_file);
}

/* Opens the free map file and reads it from disk. */
void
free_map_open (void) 
{
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_read (free_map, free_map_file))
    PANIC ("can't read free map");
}

/* Writes the free map to disk and closes the free map file. */
void
free_map_close (void) 
{
  file_close (free_map_file);
}

/* Creates a new free map file on disk and writes the free map to
   it. */
void
free_map_create (void) 
{
  /* Create inode. */
  if (!inode_create (FREE_MAP_SECTOR, bitmap_file_size (free_map), T_FILE))
    PANIC ("free map creation failed");

  /* Write bitmap to file. */
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_write (free_map, free_map_file))
    PANIC ("can't write free map");
}
