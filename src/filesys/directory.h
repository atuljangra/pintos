#ifndef FILESYS_DIRECTORY_H
//5713
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/off_t.h"
/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14


struct inode;

/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup (const struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, block_sector_t);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);
struct inode* inode_name(char *path);
struct inode* parent_inode(char *path, char *name);
bool dir_chdir(const char *name);
bool dir_mkdir(const char *name);
bool dir_isremoved(struct dir *dir);

off_t dir_tell (struct dir *dir);
void dir_seek (struct dir *dir,off_t pos);
size_t direntry_size(void);
bool dir_isDir(block_sector_t sector);
#endif /* filesys/directory.h */
