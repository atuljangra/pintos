/* This files is for managing filesys */
#ifndef VM_FILEMNGR_H
#define VM_FILEMNGR_H

#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include <stdio.h>

struct filemngr_entry
{
  struct hash_elem hash_elem;
}
#endif
