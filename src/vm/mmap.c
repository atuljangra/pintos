/*
 * For file mapping to virtual memory
 */

#include "vm/vm.h"
#include "filesys/file.h"
#include <list.h>
#include "threads/malloc.h"
#include <stdio.h>
#include "devices/block.h"
#include "lib/string.h"
#include "vm/frame.h"
#include "userprog/syscall.h"
#include "vm/sframe.h"

static mapid_t give_unique_mapid (void);
static struct mapped_entry * find_mapped_entry (mapid_t id);

/*
 * This function is used to provide a unique id to each mmaped file
 * in this process's life.
 */
static mapid_t give_unique_mapid (void)
{
  static mapid_t result = 0;
  return result++;
}

/*
 * Initialize the mmap list corresponding to the current thread
 */
void init_mmap(void)
{
  struct thread * process;
  process = thread_current ();
  process -> mapping = (struct list *) malloc (sizeof (struct list));
  list_init (process -> mapping);
}

/*
 * Called in syscall
 * Creates entries in sup_page_table and marks the present bit to be zero
 * Files are actually loaded lazily on pagefault.
 */
mapid_t vm_mmap (struct file *file, void *vaddr)
{
  //printf("u1\n");
  int size = file_length (file);
  //printf("u12\n");
  mapid_t result = MAP_FAILED;
  int pages_required = size/PGSIZE;
  if (size % PGSIZE != 0)
    pages_required++;

  struct mapped_entry *new_entry;
  new_entry = malloc (sizeof (struct mapped_entry));
  if (!new_entry)
    goto done;

  struct sup_page_table_entry *new_page_table_entry = NULL;
  int iterator = 0;

  while (iterator < pages_required)
  {
    if (find_page_by_vaddr (vaddr + iterator*PGSIZE) != NULL)
      goto done;
    iterator++;
  }
  iterator = 0;
  result = give_unique_mapid ();
  new_entry -> vaddr = vaddr;
  while (iterator < pages_required)
  {
    new_page_table_entry = vm_page_create (vaddr);
    new_page_table_entry -> file_mapped = true;
    new_page_table_entry -> frame = NULL;
    new_page_table_entry -> file = file;
    new_page_table_entry -> offset = PGSIZE * iterator;
    new_page_table_entry -> page_read_bytes = PGSIZE;
    new_page_table_entry -> writable = true;
    new_page_table_entry -> shared = false;//true;
    vaddr = vaddr + PGSIZE;
    iterator++;
  }
  new_page_table_entry -> page_read_bytes = size % PGSIZE == 0 ? PGSIZE : size % PGSIZE;
  new_entry -> id = result;
  new_entry -> file = file;
  list_push_back (thread_current () -> mapping, &new_entry -> list_elem);

  done:
  return result;
}

/*
 * Unmap the file
 * If the file is dirty then we need to rewrite the data to the disk
 */
void vm_unmap (mapid_t id)
{
  struct mapped_entry * new_entry;
  new_entry = find_mapped_entry (id);
  if (!new_entry)
    return;

  int size = file_length(new_entry -> file);
  int iterator = 0;

  int pages_mapped = size/PGSIZE;
  if (size % PGSIZE != 0)
    pages_mapped++;
  struct sup_page_table_entry * pte = NULL;
  while (iterator < pages_mapped)
  {
    pte = find_page_by_vaddr (new_entry -> vaddr + iterator*PGSIZE);
    ASSERT (pte != NULL);
    vm_page_remove (pte);
    iterator ++;
  }
  list_remove (&new_entry -> list_elem);
  file_close (new_entry -> file);
  free (new_entry);
}

/*
 * Returns mapped_entry struct corresponding to mapid_t ID
 */
static struct mapped_entry * find_mapped_entry (mapid_t id)
{
  struct list_elem *iterator;
  struct mapped_entry *result;
  for (iterator = list_begin (thread_current () -> mapping); iterator !=
       list_end (thread_current () -> mapping); iterator = list_next (iterator) )
    {
      result = list_entry (iterator, struct mapped_entry, list_elem);
      if (result -> id == id)
        return result;
    }
  return NULL;
}

/*
 * frees all the mmaped files corresponding to a current thread.
 * No need to explicitly do mmap here, we are doing that while
 * deallocating the frames.
 */
void vm_mmap_free ()
{
  struct list_elem *iterator;
  struct mapped_entry *result;
  for (iterator = list_begin (thread_current () -> mapping); iterator != list_end (thread_current () -> mapping);
       iterator = list_next (iterator) )
    {
      struct list_elem *next = list_next (iterator);
      result = list_entry (iterator, struct mapped_entry, list_elem);
      if (result -> file != NULL)
       file_close (result -> file);
      list_remove (iterator);
      free (result);
      iterator = list_prev (next);
    }
}
