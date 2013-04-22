#ifndef FILESYS_INODE_H
//29273
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define NDIRECT 128-3-2
#define NINDIRECT (BLOCK_SECTOR_SIZE / sizeof(uint32_t))
#define NDINDIRECT NINDIRECT*NINDIRECT
#define MAXFILE (NDIRECT + NINDIRECT)

struct bitmap;
enum {T_EMPTY,T_FILE,T_DIR};


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    int type;                           /* type of inode. */   
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t addrs[NDIRECT+2];              
  };


/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

void inode_init (void);
bool inode_create (block_sector_t, off_t,int type);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
bool inode_isremoved(struct inode *inode);
#endif /* filesys/inode.h */
