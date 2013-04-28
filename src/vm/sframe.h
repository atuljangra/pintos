/* This files is for managing filesys */
#ifndef VM_SFRAME_H
#define VM_SFRAME_H

#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include <stdio.h>

#define SFRAME_MAGIC 0x71583496

struct sframe
{
  /* Do not change the order and location 
     of the first two entries within the 
     structure */
  struct inode      *inode;
  off_t             offset;
  struct frame      *frame;
  struct frame      *mmapped_frame;
  struct lock       lk;
  struct hash_elem  hash_elem;
  uint32_t          magic;
};

void sframe_init (void);
struct sframe *sframe_get (struct sup_page_table_entry *);
struct sframe *sframe_create (void);
bool sframe_remove (struct sframe *sframe, struct sup_page_table_entry *spt_entry);
#endif
