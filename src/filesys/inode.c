#include "filesys/inode.h"
//6616
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/bcache.h"


static uint32_t find_block(struct inode_disk *inode, block_sector_t sector, uint32_t file_sector);
static void free_inode_data(struct inode_disk *inode);
static void inode_change_length(struct inode *inode,off_t length);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. If inode is a directory then must be called with inode->lk
   acquired. */
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos) 
{
  bool eof_flag = false;
  if(!inode_isDir(inode))
  {
    lock_acquire (&inode -> lk);
    eof_flag = true;
  }
  struct inode_disk *disk_inode = malloc(sizeof(struct inode_disk));
  read_bcache(inode->sector,disk_inode,0,sizeof (struct inode_disk));
  block_sector_t sector_id = find_block(disk_inode, inode->sector, pos/BLOCK_SECTOR_SIZE);
  free(disk_inode);
  
  if (eof_flag)
    lock_release (&inode -> lk);
    
  return sector_id;
  /*ASSERT (inode != NULL);
  if (pos < inode->data.length){
    block_sector_t sector_id = find_block(inode, pos/BLOCK_SECTOR_SIZE);
    return sector_id;
  }
  else
    return 0;*/
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
struct lock open_inode_lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&open_inode_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. There is no need
   to worry that someone else can modify the inode as it is not added
   in the directory till inode create returns. Thus, no process can use
   the file while it is being created */
bool
inode_create (block_sector_t sector, off_t length, int type)
{
  ASSERT (sizeof (struct inode_disk) == BLOCK_SECTOR_SIZE);
  //printf("creating inode\n");
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */

  uint32_t i;
  size_t sectors;
  uint32_t *sectors_allocated;

  struct inode_disk *disk_inode = malloc(sizeof(struct inode_disk));
  memset(disk_inode,0,BLOCK_SECTOR_SIZE);
  disk_inode->magic = INODE_MAGIC;
  disk_inode->type = type;
  disk_inode->length += length;
  
  sectors = bytes_to_sectors (length);
  sectors_allocated = malloc((size_t)sectors*sizeof(uint32_t));
  
  for(i=0; i<sectors; i++){
    sectors_allocated[i] = find_block(disk_inode, sector, i);
    //ASSERT((inode->data).addrs[i] == sectors_allocated[i]);
    //printf("allocted sector %d\n",sectors_allocated[i]);
    if(sectors_allocated[i] == 0){
      free (disk_inode);
      goto invalid;
    }
  }
 
 
  write_bcache (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
  
  free (disk_inode);
  success = true;
  return success;
  
  invalid:

  for(i=0; i<sectors; i++){
    if(sectors_allocated[i] == 0)
      break;
    free_map_release(sectors_allocated[i],1);
  }

  return false;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  //~ printf (" In INODE OPEN sector : %d\n", sector);
  struct list_elem *e;
  struct inode *inode;

  lock_acquire(&open_inode_lock);
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
  {
    inode = list_entry (e, struct inode, elem);
    if (inode->sector == sector) 
      {
        inode_reopen (inode);
        lock_release(&open_inode_lock);
        return inode; 
      }
  }
  

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL){
    lock_release(&open_inode_lock);
    return NULL;
  }

  /* Initialize. */
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode -> lk);
  list_push_front (&open_inodes, &inode->elem);
  lock_release(&open_inode_lock);

  //read_bcache (inode -> sector, &inode -> data, 0, BLOCK_SECTOR_SIZE);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
 
  lock_acquire(&inode->lk);
  if (inode != NULL){
    inode->open_cnt++;
  }
  lock_release(&inode->lk);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  lock_acquire(&open_inode_lock);
  inode_lock(inode);
  if (--inode->open_cnt == 0)
  {
    /* Remove from inode list and release lock. */
    list_remove (&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) 
    {
      /*free_map_release (inode->data.start,
                        bytes_to_sectors (inode->data.length)); 
      */
      struct inode_disk *ip = malloc(sizeof(struct inode_disk));
      read_bcache(inode->sector,ip,0,BLOCK_SECTOR_SIZE);
      free_inode_data(ip);
      free(ip);
      free_map_release (inode->sector, 1);
    }
   
    inode_unlock(inode);
    lock_release(&open_inode_lock);
    free (inode); 
    return;
  }

  inode_unlock(inode);
  lock_release(&open_inode_lock);

}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode_lock(inode);
  inode->removed = true;
  inode_unlock(inode);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. If inode is a 
   directory then must be called with inode->lk acquired. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      if(offset >= inode_length(inode))
        break;
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if(sector_idx == 0){
        break;
      }
        
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

     
      //~ printf ("inode read: not breaking, chunk %d, index %d \n", chunk_size, sector_idx); 
      read_bcache (sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  /*if (eof_flag)
    lock_release (&inode -> lk);
   */ 
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented. If inode is a directory then must
   be called with inode->lk acquired. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;     
  bool eof_flag = false;
  off_t initial_offset = offset;
  off_t new_length = (inode_length(inode) > size + offset)?inode_length(inode):size+offset;
  if (inode->deny_write_cnt)
    return 0;
  
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if(sector_idx == 0){
        break;
      }
      
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = new_length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
  
      write_bcache (sector_idx, (void *) (buffer + bytes_written), sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  if(!inode_isDir(inode))
  {
    lock_acquire (&inode -> lk);
    eof_flag = true;
  }
  
  /* Here Note that if some other process already extended the file then
     the size of the file need not be updated. Even though earlier this 
     process assumed earlier that file had expanded */ 
  if(inode_length(inode) < initial_offset + bytes_written){
    inode_change_length(inode, initial_offset + bytes_written);
  }
    
  if (eof_flag)
    lock_release (&inode -> lk);

  free (bounce);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire(&inode->lk);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->lk);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  lock_acquire(&inode->lk);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->lk);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  off_t length;
  read_bcache (inode -> sector, (void *)&length, sizeof(int), sizeof (off_t));
  return length;
}

bool 
inode_isremoved(struct inode *inode)
{
  ASSERT(inode != NULL);
  return inode->removed;
}

bool
inode_isDir(struct inode *inode)
{
  int type;
  read_bcache (inode -> sector, (void *)&type, 0, sizeof (int));
  if(type == T_DIR)
    return true;
  return false;
}

/* This function must be called with lock acquired on the inode */
static void 
inode_change_length(struct inode *inode, off_t length)
{
//  (inode->data).length = length;
  write_bcache (inode -> sector, (void *)&length, sizeof(int), sizeof (off_t));

}

/* 
  The contents (data) associated with each inode is stored
  in a sequence of blocks on the disk.  The first NDIRECT blocks
  are listed in ip->addrs[].  The next NINDIRECT blocks are 
  listed in the block ip->addrs[NDIRECT] and the next doubly
  indirect blocks are listed in ip->addrs[NDIRECT+1]

  Return the disk block address of the nth block in inode ip.
  If there is no such block, new block is allocated . This assumes 
  that size of file is not less than the file_sector*sector_size */
static uint32_t
find_block(struct inode_disk *inode, block_sector_t sector, uint32_t file_sector)
{
  //ASSERT((uint32_t)(ip->data).length > file_sector*BLOCK_SECTOR_SIZE);
  
  //printf("in find_block %d\n",file_sector);
  
  uint32_t addr, *buffer = malloc(BLOCK_SECTOR_SIZE);
  //struct inode_disk *inode = &ip->data;

	//cprintf("block number required: %d\n", bn);
  if(file_sector < NDIRECT){
    if((int)inode->addrs[file_sector] == 0){
			//printf("allocating bmap\n");
      if(!free_map_allocate(1,&inode->addrs[file_sector])){
        free(buffer);
        return 0;
      }
      //printf("returning allocated sector %d\n",inode->addrs[file_sector]);
      write_bcache (sector,inode, 0, BLOCK_SECTOR_SIZE);
      
      addr = inode->addrs[file_sector];
      memset(buffer,0,BLOCK_SECTOR_SIZE);
      write_bcache (addr,buffer, 0, BLOCK_SECTOR_SIZE);
		}	
 
    free(buffer);
    
    return inode->addrs[file_sector] ;
  }

     
  file_sector -= NDIRECT;

  if(file_sector < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if(inode->addrs[NDIRECT] == 0){
      if(!free_map_allocate(1,&inode->addrs[NDIRECT])){
        free(buffer);
        return 0;
      }
      
      write_bcache (sector,inode, 0, BLOCK_SECTOR_SIZE);
      memset(buffer,0,BLOCK_SECTOR_SIZE);
    }
    else
      read_bcache(inode->addrs[NDIRECT], buffer, 0, BLOCK_SECTOR_SIZE);

      
    if(buffer[file_sector] == 0){
      if(!free_map_allocate(1,&buffer[file_sector])){
        free(buffer);
        return 0;
      }
        
      write_bcache (inode->addrs[NDIRECT],buffer, 0, BLOCK_SECTOR_SIZE);
      
      addr = buffer[file_sector];
      memset(buffer,0,BLOCK_SECTOR_SIZE);
      write_bcache (addr,buffer, 0, BLOCK_SECTOR_SIZE);
    }
    else
      addr = buffer[file_sector];
    //brelse(bp);
 
    free(buffer);
    return addr;
  }
  
  //PANIC("NEVER SHOULD HAVE COME HERE");
 
  
  file_sector -= NINDIRECT;
  
  if(file_sector < NDINDIRECT){
    if(inode->addrs[NDIRECT+1] == 0){
      if(!free_map_allocate(1,&inode->addrs[NDIRECT+1])){
        free(buffer);
        return 0;
      }
      write_bcache (sector,inode, 0, BLOCK_SECTOR_SIZE);
      memset(buffer,0,BLOCK_SECTOR_SIZE);
    }
    else
      read_bcache (inode->addrs[NDIRECT+1], buffer, 0, BLOCK_SECTOR_SIZE);
    
    uint32_t first_level = file_sector/NINDIRECT;
    uint32_t second_level = file_sector%NINDIRECT;
   // printf("levels are %d,%d,%d\n",first_level,second_level,file_sector);
  //  PANIC("NEVER SHOULD HAVE COME HERE levels are %d,%d,%d\n",first_level,second_level,file_sector);
    addr = buffer[first_level];
    if(addr == 0){
      if(!free_map_allocate(1,&buffer[first_level])){
        free(buffer);
        return 0;
      }
      write_bcache (inode->addrs[NDIRECT+1],buffer, 0, BLOCK_SECTOR_SIZE);
      addr = buffer[first_level];
      memset(buffer,0,BLOCK_SECTOR_SIZE);
    }
    else
      read_bcache (addr, buffer, 0, BLOCK_SECTOR_SIZE);
      
    
    if(buffer[second_level] == 0){
      if(!free_map_allocate(1,&buffer[second_level])){
        free(buffer);
        return 0;
      }
      write_bcache (addr,buffer, 0, BLOCK_SECTOR_SIZE);
      addr = buffer[second_level];
      memset(buffer,0,BLOCK_SECTOR_SIZE);
      write_bcache (addr,buffer, 0, BLOCK_SECTOR_SIZE);
    }
    else
      addr = buffer[second_level];

    free(buffer);
    return addr;
  }
  PANIC("bmap: out of range");
}

/* Frees all the sectors used by inode to use for storing its 
   data. It parses through all direct,indirect and doubly indirect 
   links to free the data blocks */
static void
free_inode_data(struct inode_disk *inode)
{

  int i;
  uint32_t *buffer, *buffer2;
  buffer = malloc(BLOCK_SECTOR_SIZE);
  
  for(i=0; i<NDIRECT; i++){
    if(inode->addrs[i]){
      free_map_release(inode->addrs[i],1);
      inode->addrs[i] = 0;
    }
  }
  if(inode->addrs[NDIRECT]){
    read_bcache (inode->addrs[NDIRECT], buffer, 0, BLOCK_SECTOR_SIZE);
    for(i=0; i<(int)NINDIRECT ;i++){
      if(buffer[i]){
        free_map_release(buffer[i], 1);
      }
    }
    free_map_release(inode->addrs[NDIRECT],1);
    inode->addrs[NDIRECT] = 0;
  }
  if(inode->addrs[NDIRECT+1]){
    //PANIC("NEVER SHOULD HAVE COME HERE");
    buffer2 = malloc(BLOCK_SECTOR_SIZE);
    read_bcache (inode->addrs[NDIRECT+1], buffer, 0, BLOCK_SECTOR_SIZE);
    for(i=0; i<(int)NINDIRECT ;i++){
      if(buffer[i]){
        read_bcache (buffer[i], buffer2, 0, BLOCK_SECTOR_SIZE);
        int j;
        for(j=0; j<(int)NINDIRECT; j++){
          if(buffer2[j]){
            free_map_release(buffer2[j], 1);
          }
        }
        free_map_release(buffer[i], 1);
      }
    }
    free_map_release(inode->addrs[NDIRECT+1],1);
    inode->addrs[NDIRECT+1] = 0;
    free(buffer2);
  }
  free(buffer);
}

void
inode_lock (struct inode * inode)
{
  lock_acquire (&inode -> lk);
}

void
inode_unlock (struct inode * inode)
{
  lock_release (&inode -> lk);
}
