#include "filesys/directory.h"
//24028
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/free-map.h"
#include "filesys/bcache.h"
static char* skipelem(char *path, char **name);
static bool dir_isEmpty(struct dir *dir);



/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };
  
/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure.*/
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry),T_DIR);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure.
   The first two directory entries are "." and ".." so the directory
   position is initialized after default entries */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 2*sizeof(struct dir_entry);
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
	struct inode *node = inode_open (ROOT_DIR_SECTOR);
  struct dir* root = dir_open (node);
	return root;
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
  {
    inode_close (dir->inode);
    free (dir);
  }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}



/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  inode_lock (dir -> inode);
  if (lookup (dir, name, &e, NULL)){
    
    /* Inode open cannot be called if trying to open cwd as reopen 
       tries to reaquire lock on directory inode */
    if(inode_get_inumber(dir->inode) == e.inode_sector){
      inode_unlock (dir -> inode);
      *inode = inode_reopen (dir->inode);
      return *inode != NULL;
    }
    *inode = inode_open (e.inode_sector);
  }
  else
    *inode = NULL;

  inode_unlock (dir -> inode);

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  inode_lock (dir -> inode);
  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:  
  inode_unlock (dir -> inode);
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME.
   When trying to remove a directory, if it is not empty i.e. it 
   contains other than default directory entries then false is returned.
   The directory cannot be removed */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);


  inode_lock (dir -> inode);
  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;
  
  /* Open inode. This must not be called to open the directory 
     as it will try to reaquire lock on directory */
  inode = inode_open (e.inode_sector);
  
    
  if (inode == NULL)
    goto done;

  if(inode_isDir(inode) && !dir_isEmpty(dir_open(inode))){
    goto done;
  }
  
  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;


  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_unlock (dir -> inode);
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;
  inode_lock (dir -> inode);
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
  {
    dir->pos += sizeof e;
    if (e.in_use)
    {
      strlcpy (name, e.name, NAME_MAX + 1);
      inode_unlock (dir -> inode);
      return true;
    } 
  }
  inode_unlock (dir -> inode);
  return false;
}

/* Given the path ,verifies if the path of an existing
   directory is given.If not then false is returned.
   Otherwise the current working directory is changed
   and returns true */
bool
dir_chdir(const char *name)
{
  ASSERT(dir_isDir(thread_current()->cwd));
 
  
  char *path = malloc(strlen(name)+1);
  char *temp = path;
  memcpy(path,name,strlen(name)+1);
  struct inode *inode = NULL;
	inode = inode_name(path);
  
  struct dir *dir = dir_open(inode);
  if(dir == NULL)
    return false;
    
  struct thread *cur = thread_current();
  dir_close(cur->cwd);
  cur->cwd = dir;
  free(temp);
  ASSERT(dir_isDir(thread_current()->cwd));
  return true;
}

bool 
dir_mkdir(const char *name)
{
  ASSERT(dir_isDir(thread_current()->cwd));
  
  block_sector_t inode_sector = 0;
  char *path = malloc(strlen(name)+1);
  char *temp1 = path;
  char *x = malloc(strlen(name)+1);
  char *temp2 = x;
  memcpy(path,name,strlen(name)+1);
  
  struct inode *inode = parent_inode(path,x);
  struct dir *dir = dir_open(inode);
  bool success = false;
  if(dir!=NULL && dir_isremoved(dir)){
    success = false;
    goto done;
  }

  success = ( dir != NULL
              && free_map_allocate (1, &inode_sector)
              && dir_create (inode_sector, 16));
  if(success){
    /* dir_init must succeed */
    success =  (dir_add (dir, x, inode_sector)
              && dir_init(inode_sector,inode_get_inumber(inode)));
    if(!success){
      struct inode_disk *inode = malloc(sizeof(struct inode_disk));
      read_bcache(inode_sector, inode, 0, BLOCK_SECTOR_SIZE);
      free_inode_data(inode);
      free(inode);
    }
  }

  
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  done:
  dir_close (dir);
  free(temp1);
  free(temp2);
  ASSERT(dir_isDir(thread_current()->cwd));
  return success;
}

bool 
dir_isremoved(struct dir *dir)
{
  ASSERT(dir != NULL);
  return dir->inode->removed;
}

off_t
dir_tell (struct dir *dir)
{
  ASSERT(dir != NULL);
  return dir->pos;
}
void
dir_seek (struct dir *dir,off_t pos)
{
  ASSERT(dir != NULL);
  lock_acquire(&dir_get_inode(dir)->lk);
  if(pos > 0)
    dir->pos = pos;
  lock_release(&dir_get_inode(dir)->lk);
}

size_t 
direntry_size(void)
{
  return sizeof(struct dir_entry);
}
/* Copy the next path element from path into name.
   Return a pointer to the element following the copied one.
   The returned path has no leading slashes,
   so the caller can check *path=='\0' to see if the name is the last one.
   If no name to remove, return 0.
   Examples:
   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
   skipelem("///a//bb", name) = "bb", setting name = "a"
   skipelem("a", name) = "", setting name = "a"
   skipelem("", name) = skipelem("////", name) = 0 */
static char*
skipelem(char *path, char **name1)
{

  char *s;
  int len;
  char *name = *name1;
  while(*path == '/')
    path++;
  if(*path == 0){
    *name = 0;
    return 0;
  }
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len > NAME_MAX){
    *name1 = 0;
  }
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;


  return path;
}

/* Look up and return the inode for a path name.
   If nameiparent != 0, return the inode for the parent and copy the final
   path element into name, which must have room for NAME_MAX+1 bytes.
   If nameiparent == 0 then return the inode corresponding to the given path */
static struct inode*
get_name(char *path, int nameiparent, char *name)
{
  struct dir *dir;
  struct inode *ip = NULL, *next = NULL ;
  struct thread *t = thread_current();

  if(*path == '/'){
    dir = dir_open_root();
    ip = inode_reopen(dir->inode);
    next = ip;
  }
  else{
    ASSERT(t->cwd != NULL);
    if(t->cwd == NULL)
      return NULL;
    dir = dir_reopen(t->cwd);
  }
  
  ip = dir_get_inode(dir);
  while((path = skipelem(path, &name)) != 0){
    if(!inode_isDir(ip)){
      dir_close(dir);
      inode_close(ip);
      return NULL;
    }
    if(nameiparent && *path == '\0'){
      if(ip == dir->inode)
        ip = inode_reopen(ip);
      dir_close(dir);
      return ip;
    }
    if((!dir_lookup(dir_open(ip), name, &next)) ){
      dir_close(dir);
      inode_close(ip);    
      return NULL;
    }
    if(ip != dir->inode)
      inode_close(ip);
    ip = next;
  }
  if(nameiparent){
    dir_close(dir);
    inode_close(ip);
    return NULL;
  }
  if(name == 0 || (*name == 0 && next == NULL)){
    dir_close(dir);
    inode_close(ip);
    return NULL;
  }
  
  dir_close(dir);
  return ip;
}

struct inode*
inode_name(char *path)
{
  char name[NAME_MAX+1];
  return get_name(path, 0, name);
}

struct inode*
parent_inode(char *path, char *name)
{
  return get_name(path, 1, name);
}

bool
dir_init(block_sector_t sector,block_sector_t parent)
{
    struct dir *dir = dir_open(inode_open(sector));
    ASSERT(dir_isDir(dir));

    bool status = dir_add(dir, "..",parent);
    if(status)
      status = dir_add(dir,".",sector);
    
    dir_close(dir);
    return status;
}

static bool
dir_isEmpty(struct dir *dir)
{
  struct dir_entry e;
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
  {
    dir->pos += sizeof e;
    if (e.in_use)
      {
        return false;
      } 
  }
  return true;
}

bool 
dir_isDir(struct dir *dir)
{
  return inode_isDir(dir->inode);
}
