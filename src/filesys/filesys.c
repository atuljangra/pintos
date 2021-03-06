#include "filesys/filesys.h"
//7227
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/bcache.h"
/* Partition that contains the file system. */
// struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  /* initialize the buffer cache */
  init_bcache ();
  
  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();
  struct thread *cur = thread_current();
  cur->cwd = dir_open_root ();
  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  //~ print_my_ass ();
  flush_buffer_cache ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{

  
  block_sector_t inode_sector = 0;
  char *path = malloc(strlen(name)+1);
  char *temp1 = path;
  char *x = malloc(strlen(name)+1);
  char *temp2 = x;
  memcpy(path,name,strlen(name)+1);
  
  struct inode *inode = parent_inode(path,x);
  struct dir *dir = dir_open(inode);
  ASSERT(dir_isDir(dir));
  bool success = false;
  if(dir!=NULL && dir_isremoved(dir)){
    success = false;
    goto done;
  }
  
  //struct dir *dir = dir_open_root ();
  success = ( dir != NULL
              && free_map_allocate (1, &inode_sector)
              && inode_create (inode_sector, initial_size, T_FILE));
  if(success){
    success = dir_add (dir, x, inode_sector);
    if(!success){
      struct inode_disk *inode = malloc(sizeof(struct inode_disk));
      read_bcache(inode_sector, inode, 0, BLOCK_SECTOR_SIZE);
      free_inode_data(inode);
      free(inode);
    }
  }
 // printf("inode number %d\n",inode_sector);
  
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

//ASSERT(success);
  done:
  dir_close (dir);
  free(temp1);
  free(temp2);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if(dir_isremoved(thread_current()->cwd)){
    if(!memcmp(name,".",strlen(name)))
      return NULL;
    if(!memcmp(name,"..",strlen(name)))
      return NULL;
  }
  char *path = malloc(strlen(name)+1);
  char *temp = path;
  memcpy(path,name,strlen(name)+1);

  struct inode *inode = NULL;
	inode = inode_name(path);
  free(temp);
  struct file *file = file_open (inode);
  if(file!=NULL && inode_isDir(inode)){
    file_seek(file,2*direntry_size());
  }
  /*struct inode *inode = malloc(sizeof(struct inode));
  bool status = dir_lookup(thread_current()->cwd,name,&inode);
  struct file *file = NULL;
  ASSERT(status);
  file = file_open(inode);*/
  return file;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  char *path = malloc(strlen(name)+1);
  char *temp1 = path;
  char *x = malloc(strlen(name)+1);
  char *temp2 = x;
  memcpy(path,name,strlen(name)+1);
  
  struct inode *inode = parent_inode(path,x);
  struct dir *dir = dir_open(inode);
  
  bool success = dir != NULL && dir_remove (dir, x);
  dir_close (dir); 
  free(temp1);
  free(temp2);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  if(!dir_init(ROOT_DIR_SECTOR,ROOT_DIR_SECTOR))
    PANIC("root directory initialization failed");
  free_map_close ();
  printf ("done.\n");
}
