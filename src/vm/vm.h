/*
 * Main file that manages supplemental page_table and mmaped table.
 * Contains all the interfaced functions, structs and macros.
 * individual mmap.h, page.h
 */
#ifndef VM_VM_H
#define VM_VM_H

#include "threads/pte.h"
#include "lib/kernel/hash.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include <stdio.h>

void init_frame (void);
struct frame_table_entry * vm_alloc_frame (void);
void vm_remove_frame (void *);

void sup_pt_init (void);
struct sup_page_table_entry *vm_page_create (void *vaddr);
struct sup_page_table_entry *vm_create_page_and_alloc (void *vaddr);
void vm_page_destroy (struct sup_page_table_entry *pt_entry UNUSED);
struct sup_page_table_entry * find_page_by_vaddr (void *vaddr);
void sup_page_table_destroy (struct hash *sup_pt);
bool sup_page_table_load (struct sup_page_table_entry *spt_entry);
void vm_page_remove (struct sup_page_table_entry *sup_pt);

void init_mmap (void);
mapid_t vm_mmap (struct file *file, void *vaddr);
void vm_unmap (mapid_t id);
void vm_mmap_free (void);
//void vm_demand_mapping (struct sup_page_table_entry *sup, uint32_t *pd);
#endif
