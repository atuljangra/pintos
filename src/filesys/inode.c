#include "filesys/inode.h"
//6616
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"



static uint32_t find_block(struct inode *inode, uint32_t file_sector);
static void free_inode_data(struct inode *ip);


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
   POS. */
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return find_block(inode, pos/BLOCK_SECTOR_SIZE);
  else
    return 0;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, int type)
{
  ASSERT (sizeof (struct inode_disk) == BLOCK_SECTOR_SIZE);
  //printf("creating inode\n");
  bool success = false;

  ASSERT (length >= 0);
  struct inode *inode = malloc(sizeof(struct inode));
  memset(&inode->data,0,BLOCK_SECTOR_SIZE);
  inode->sector = sector;
  struct inode_disk *disk_inode = &inode->data;
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */

  uint32_t i;
  size_t sectors;
  uint32_t *sectors_allocated;
  if (inode != NULL)
    {
      sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->type = type;
      sectors_allocated = malloc((size_t)sectors*sizeof(uint32_t));
      
      //printf("sectors %d\n",sectors);
      for(i=0; i<sectors; i++){
        sectors_allocated[i] = find_block(inode, i);
        //ASSERT((inode->data).addrs[i] == sectors_allocated[i]);
        //printf("allocted sector %d\n",sectors_allocated[i]);
        if(sectors_allocated[i] == 0)
          goto invalid;
      }
      block_write (fs_device, sector, disk_inode);
      success = true;
      
    }
  ASSERT(success);
 // printf("creation done\n");
  free (inode);
  return success;
  
  invalid:
  //printf("unable to allocate\n");

  for(i=0; i<sectors; i++){
    if(sectors_allocated[i] == 0)
      break;
    free_map_release(sectors_allocated[i],1);
  }
  free(inode);
  return false;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
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
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          /*free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
          */
          free_inode_data(inode);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if(sector_idx == 0)
        return 0;
        
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  
  if(inode_length(inode) < offset + size){
    (inode->data).length = offset + size;
    block_write(fs_device,inode->sector,&inode->data);
    //printf("new file size %d\n",(inode->data).length);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if(sector_idx == 0)
        return 0;
      
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
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
  if((inode->data).type == T_DIR)
    return true;
  return false;
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
find_block(struct inode *ip, uint32_t file_sector)
{
  ASSERT((uint32_t)(ip->data).length > file_sector*BLOCK_SECTOR_SIZE);
  
  //printf("in find_block %d\n",file_sector);
  
  uint32_t addr, *buffer = malloc(BLOCK_SECTOR_SIZE);
  struct inode_disk *inode = &ip->data;

	//cprintf("block number required: %d\n", bn);
  if(file_sector < NDIRECT){
    if((int)inode->addrs[file_sector] == 0){
			//printf("allocating bmap\n");
      if(!free_map_allocate(1,&inode->addrs[file_sector])){
        free(buffer);
        return 0;
      }
      //printf("returning allocated sector %d\n",inode->addrs[file_sector]);
      block_write(fs_device,ip->sector,inode);
      
      addr = inode->addrs[file_sector];
      memset(buffer,0,BLOCK_SECTOR_SIZE);
      block_write(fs_device,addr,buffer);
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
      
      block_write(fs_device,ip->sector,inode);
      memset(buffer,0,BLOCK_SECTOR_SIZE);
    }
    else
      block_read(fs_device, inode->addrs[NDIRECT], buffer);
      
    if(buffer[file_sector] == 0){
      if(!free_map_allocate(1,&buffer[file_sector])){
        free(buffer);
        return 0;
      }
        
      block_write(fs_device,inode->addrs[NDIRECT],buffer);
      
      addr = buffer[file_sector];
      memset(buffer,0,BLOCK_SECTOR_SIZE);
      block_write(fs_device,addr,buffer);
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
      block_write(fs_device,ip->sector,inode);
      memset(buffer,0,BLOCK_SECTOR_SIZE);
    }
    else
      block_read(fs_device, inode->addrs[NDIRECT+1], buffer);
    
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
      block_write(fs_device,inode->addrs[NDIRECT+1],buffer);
      addr = buffer[first_level];
      memset(buffer,0,BLOCK_SECTOR_SIZE);
    }
    else
      block_read(fs_device, addr, buffer);
      
    
    if(buffer[second_level] == 0){
      if(!free_map_allocate(1,&buffer[second_level])){
        free(buffer);
        return 0;
      }
      block_write(fs_device,addr,buffer);
      addr = buffer[second_level];
      memset(buffer,0,BLOCK_SECTOR_SIZE);
      block_write(fs_device,addr,buffer);
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
free_inode_data(struct inode *ip)
{
  struct inode_disk *inode = &ip->data;
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
    block_read(fs_device, inode->addrs[NDIRECT], buffer);
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
    block_read(fs_device, inode->addrs[NDIRECT+1], buffer);
    for(i=0; i<(int)NINDIRECT ;i++){
      if(buffer[i]){
        block_read(fs_device, buffer[i], buffer2);
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
