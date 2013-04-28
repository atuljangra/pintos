#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "vm/frame.h"
#include "threads/vaddr.h"

void swap_init (void);
bool swap_out (struct frame *);
void swap_in (struct frame *, struct frame *);
void swap_free (void *);
bool has_swap (void);
#endif
