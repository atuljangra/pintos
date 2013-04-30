
//14406
#include <stdio.h>
#include <round.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "lib/syscall-nr.h"
#include "userprog/syscall.h"
#include "userprog/syscall.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "lib/string.h"
#include "devices/input.h"
#include "vm/vm.h"



static void syscall_handler (struct intr_frame *);
static int getb_user (const uint32_t *uaddr, bool *is_valid);
static int getl_user (const uint32_t *uaddr, bool *is_valid) UNUSED;
static bool putb_user (uint32_t *udst, uint8_t byte_val) UNUSED;
static bool putl_user (uint32_t *udst, uint32_t long_val) UNUSED;

void on_pgfault (void) NO_RETURN;
bool validate_buffer (const char *buffer, char *terminator, int max_len, bool write);

/*  Checks whether the virtual memory from buffer(included) to
 *  BUFFER+MAX_LEN(excluded) is accessible by the current thread
 *  or not. If a terminator is found before BUFFER+MAX_LEN then
 *  the mapping is checked only upto the first terminator,
 *  including the terminator. If terminator is not found than false
 *  is returned. If the terminator was NULL then mapping is checked upto
 *  BUFFER+MAX_LEN
 *  if WRITE is true that means we are going to write onto the buffer.
 *  Note that the buffer gets changed when validating a write buffer.
 */ 
bool validate_buffer (const char *buffer, char *terminator, int max_len, bool write)
{
  char *addr;
  uint8_t result;
  if (max_len == 0)
    return true;
  bool is_valid, term_p = terminator != NULL ? true : false;
  if (!term_p)
  {
    for (addr = (char *) buffer; (uint32_t)addr < 
          (uint32_t) ROUND_UP( ( (uint32_t)( (char *) buffer + max_len ) ), PGSIZE); addr +=  PGSIZE)
    {
      result = getb_user((uint32_t *) addr, &is_valid);
      /* If read returned an error. */
      if (!is_valid)
        return false;
      /* If read was successful, continue to next read. */
      if (!write)
        continue;
      /* If this is a write buffer, try to write to validate. */
      is_valid = putb_user((uint32_t *) addr, 0x0);
      /* If write was successful, write back the value to repair the damage. */
      if (is_valid)
        is_valid = putb_user((uint32_t *) addr, (uint8_t) result);
      /* If write was unsuccessful return false. */
      if (!is_valid)
        return false;
    }
    return true;
  }
  else
  {
    for (addr = (char *) buffer; addr < (char *) buffer + max_len; addr++)
    {
      result = getb_user((uint32_t *) addr, &is_valid);
      /* If reading returned an error. */
      if (!is_valid)
        return false;
      char c = (char) result;
      bool term_found = strchr(terminator, c) != NULL ;
      /* If terminator has been found and this is a read buffer, return true. */
      if (!write && term_found)
        return true;
      /* If terminator has not yet been found and this is a read buffer continue. */
      else if (!write)
        continue;
      /* Here, only if checking for a write buffer. */
      /* Try putting a value at addr. */
      is_valid  = putb_user((uint32_t *) addr, 0x0);
      /* If the value was successfully put, means we have clobbered the buffer, repair it. */
      if (is_valid)
        is_valid = putb_user((uint32_t *) addr, result);
      /* write was valid and the terminator has been found return true. */
       if (is_valid && term_found)
        return true;
       /* If terminator has not been found yet, continue. */
      else if (is_valid)
        continue;
      return false;
    }
    return false;
  }
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
 // lock_init (&file_lock);
}

/*
 * return: -1 if buffer not valid,
 * 0 if file not valid
 * otherwise RESULT (actual size that is written
 */
int write (int fd, const void *buffer, unsigned length)
{
  int result;
  int ret = 0;
 // lock_acquire(&file_lock);
  //~ printf ("yaha hu main \n");
  if (fd < 0 || (fd == STDIN_FILENO && thread_current() -> fd_std_in) 
      || (fd == 2 && thread_current() -> fd_std_err))
  {
    return -1;
  }
  bool valid = validate_buffer (buffer, NULL, length, false);
  if (!valid)
  {
  //  lock_release(&file_lock);
    //~ printf ("kaha hu main \n");
    on_pgfault ();
    NOT_REACHED ();
  }

  int num_of_pages = length / PGSIZE;
  int to_write = length;
  int cur_write = 0;
  int i = 0;
  //~ printf ("no: %d, to_write %d", num_of_pages, to_write);
  void *kpage = palloc_get_page (PAL_ASSERT);
  for (i = 0; i <= num_of_pages; i++)
  {
    if (to_write / PGSIZE == 0)
      to_write = to_write % PGSIZE;

    if (to_write == 0)
    {
      //~ printf ("exiting %d \n", i);
      break;
    }

    cur_write = (to_write > PGSIZE) ? PGSIZE : to_write;

    memcpy (kpage, buffer + (length - to_write), cur_write);
    if (fd == STDOUT_FILENO && thread_current() -> fd_std_out)
    {
      int limit = 256;
      int i = cur_write / limit;
      while (i > 0)
      {
        putbuf (kpage, limit);
        kpage += limit;
        i--;
      }
      putbuf (kpage, cur_write % limit);
      result = cur_write;
    //~ printf (" WRITTEN console: %d", result);
    }
    else
    {
      if (fd < 0)
      {
        palloc_free_page (kpage);
        return 0;
      }
      
      struct fd_table_element * fd_elem = find_file (fd);
      if (fd_elem == NULL)
      {
        palloc_free_page (kpage);
        return 0;
      }
      struct file *write = fd_elem -> file_name;
      if(write == NULL)
      {
        ret = -1;
        palloc_free_page (kpage);
        return ret;
      }
      if(inode_isDir(file_get_inode(write)))
      {
        ret = -1;
        palloc_free_page (kpage);
        return ret;
      }      
      result = file_write (write, kpage, cur_write);
//    printf (" WRITTEN : %d", result);
  }
  to_write = to_write - cur_write;
  ret += result;
  }
  palloc_free_page (kpage);
  return ret;
}

/*
 * return -1 if FILE is not valid
 */
int open (const char *file)
{
 // lock_acquire(&file_lock);
  bool valid;
  int fd = 0;
  struct file* file_open;
  valid = validate_buffer (file, "", PGSIZE, false);

  if (!valid)
  {
  //  lock_release(&file_lock);
    thread_exit (-1);
  }
  file_open = filesys_open (file);
  if (file_open == NULL)
  {
    fd = -1;
    goto done;
  }
  fd = add_file(file_open);
  done:
   // lock_release(&file_lock);
  return fd;
}

/*
 * no error caused in this
 * Thread_exit while closing stdout???
 */
void close (int fd)
{
 // lock_acquire(&file_lock);
  if (fd < 0)
  //goto done;
  return;
  if ((fd == STDOUT_FILENO && thread_current() -> fd_std_out) 
      || (fd == STDIN_FILENO && thread_current() -> fd_std_in) 
      || (fd == 2 && thread_current() -> fd_std_err)){
       // lock_release(&file_lock);
        thread_exit(-1);
        
  }
  
  struct fd_table_element *fd_elem = find_file (fd);
  if (fd_elem == NULL)
  //  goto done;
    return;
  else
  {
    file_close (fd_elem -> file_name);
    list_remove (&fd_elem -> file_elem);
  }
    entry_remove (fd);
   // lock_release(&file_lock);
}

/* If the fd corresponds to directory then it must fail */ 
int read (int fd, void* buffer, unsigned length)
{
//  lock_acquire(&file_lock);
  int result = 0;
  int ret = 0;
  if (fd < 0 || (fd == STDOUT_FILENO && thread_current() -> fd_std_out) 
      || (fd == 2 && thread_current() -> fd_std_err))
  {
    return -1;
  }
  //~ if (length = PGSIZE)
  //~ printf ("Read : fd %d length %d \n", fd, length);
  int num_of_pages = length/PGSIZE;
  int to_read = length;
  int cur_read = 0;
  int i = 0;
  bool valid;
  valid = validate_buffer (buffer, NULL, length, true);
  if (!valid)
  {
    //thread_exit (-1);
    on_pgfault ();
    NOT_REACHED ();
  }
  void *kpage = palloc_get_page (PAL_ASSERT);
  //~ printf ("Read : fd %d length %d no: %d\n", fd, length, num_of_pages);
  for (i = 0; i <= num_of_pages; i++)
  {
    if (to_read / PGSIZE == 0)
      to_read = to_read % PGSIZE;

    if (to_read == 0)
    {
      //~ printf ("break in iteration %d \n", i);
      //palloc_free_page (kpage);
      //~ printf ("exiting %d res %d\n", fd, ret); 
      break;
    }
    cur_read = (to_read > PGSIZE) ? PGSIZE : to_read;
    // printf ("Reading : fd %d length %d cur %d  toread %d validating %d \n", fd, length, cur_read, to_read, (length - to_read));
    if (fd == STDIN_FILENO && thread_current() -> fd_std_in)
    {
      int i = 0;
      while (i < cur_read)
        *(uint8_t *)(kpage + i) = input_getc();
       result = cur_read;
      // continue;
    }
    else
    {
      struct fd_table_element *fd_elem = find_file (fd);
      if (fd_elem == NULL)
      {
        palloc_free_page (kpage);
        return -1;
      }
      struct file * read = fd_elem -> file_name;
      if(read == NULL)
      {
        palloc_free_page (kpage);
        return -1;
      }
      if(inode_isDir(file_get_inode(read)))
      {
        palloc_free_page (kpage);
        return -1;
      }
      result = file_read (read, kpage, cur_read);
      //~ printf ("Read %d from fd %d length%d\n", result, fd, length);
    }
    memcpy (buffer + (length - to_read), kpage, cur_read);
    to_read = to_read - cur_read;
    ret += result;
  }
  palloc_free_page (kpage);
  return ret;
}

/* thread exit? is it that serious? */
bool create (const char *file_name, unsigned initial_size)
{
 // lock_acquire(&file_lock);
  bool result, valid; 
  valid = validate_buffer (file_name, "", PGSIZE, false);
  if (!valid || strnlen (file_name, 32) == 0)
  {
   // lock_release (&file_lock);
    thread_exit (-1);
    NOT_REACHED ();
  }
  result =  filesys_create (file_name, initial_size);
 // lock_release(&file_lock);
  return result;
}

bool remove (const char *file_name)
{
/*  printf("remove %s\n", file_name);*/
 // lock_acquire(&file_lock);
  bool result, valid;
  valid = validate_buffer (file_name, "", PGSIZE, false);
  if (!valid)
  {
    result = false;
    goto done;
  }
  result = filesys_remove (file_name);
  done:
   // lock_release(&file_lock);
  return result;
}

int filesize (int fd)
{
 // lock_acquire(&file_lock);
  int result;
  if (fd < 0)
  {
    result = 0;
    goto done;
  }
  struct fd_table_element *fd_elem = find_file (fd);
  if (fd_elem == NULL)
  {
    result = 0;
    goto done;
  }
  result = file_length (fd_elem -> file_name);
  done:
   // lock_release(&file_lock);
  return result;
}

void seek (int fd, unsigned position)
{
 // lock_acquire(&file_lock);
  if ( fd < 0)
   // goto done;
   return;
   //    printf ("am I here???"); 
    struct fd_table_element *fd_elem = find_file (fd);
    if (fd_elem != NULL)
      file_seek (fd_elem -> file_name, position);
 // lock_release(&file_lock);  
}

unsigned tell (int fd)
{
 // lock_acquire(&file_lock);
  unsigned result;
  if (fd < -1)
  {
    result = 0;
    goto done;
  }
  struct fd_table_element *fd_elem = find_file (fd);
  if (fd_elem == NULL)
  {
    result = 0;
    goto done;
  }
  result = file_tell (fd_elem -> file_name);
  done:
  //  lock_release(&file_lock);
  return result;

}

mapid_t mmap (int fd, void *addr)
{
  //printf ("mmap was called\n");
  if (!addr)
    return -1;
  if ((fd == STDOUT_FILENO && thread_current() -> fd_std_out) 
      || (fd == STDIN_FILENO && thread_current() -> fd_std_in) 
      || (fd == 2 && thread_current() -> fd_std_err))
    return -1;
  struct fd_table_element *fd_elem = find_file (fd);
  if (fd_elem == NULL)
    return -1;
  if ((uint32_t)addr % PGSIZE != 0) /* not aligned properly*/
    return -1;
  struct file* file = file_reopen (fd_elem -> file_name);
  ASSERT(file != NULL)
  mapid_t result = vm_mmap (file, addr); 
  return result;
}

void munmap (mapid_t mapping)
{
  vm_unmap (mapping);
  return;
}

bool 
chdir(const char *file_name)
{
//  lock_acquire(&file_lock);
  bool result, valid;
  valid = validate_buffer (file_name, "", PGSIZE, false);
  if (!valid)
  {
    result = false;
    goto done;
  }
  result = dir_chdir(file_name);

  done:
  //  lock_release(&file_lock);
  return result;
}

bool 
mkdir(const char *file_name)
{
//  lock_acquire(&file_lock);
  bool result, valid;
  valid = validate_buffer (file_name, "", PGSIZE, false);
  if (!valid)
  {
    result = false;
    goto done;
  }
  result = dir_mkdir(file_name);
  done:
   // lock_release(&file_lock);
  return result;
}

bool
readdir(int fd, char *file_name)
{
  //printf("inside readdir\n");

 // lock_acquire(&file_lock);
  bool result = 0;
  if (fd < 0 || (fd == STDOUT_FILENO && thread_current() -> fd_std_out) 
      || (fd == 2 && thread_current() -> fd_std_err)
      || (fd == STDIN_FILENO && thread_current() -> fd_std_in))
  {
    result = false;
    goto done;
  }
  bool valid;
  valid = validate_buffer (file_name, NULL, NAME_MAX+1 , true);
  if (!valid)
  {
    printf("buffer invalid\n");
  //  lock_release (&file_lock);
    thread_exit (-1);
    NOT_REACHED ();
  }

  struct fd_table_element *fd_elem = find_file (fd);
  if (fd_elem == NULL){
    printf("fd not found\n");
    goto done;
  }
  struct file * read = fd_elem -> file_name;
  if (read == NULL)
  {
    printf("file empty\n");
    result = false;
    goto done;
  }
  struct inode *inode = file_get_inode(read);
  struct dir *dir = dir_open(inode);
  if(!inode_isDir(inode)){
    //printf("not dir\n");
    result = false;
    goto done;
  }
  
  
  dir_seek(dir,file_tell(read));
  result = dir_readdir (dir, file_name);
  //result = true;
  if(result){
    file_seek(read,dir_tell(dir));
  }
  done:
  //dir_close(dir);
//  lock_release(&file_lock);
    
  return result;
  
}

bool
isdir(int fd)
{
//  lock_acquire(&file_lock);
  bool result = false;
  if (fd < 0 || (fd == STDOUT_FILENO && thread_current() -> fd_std_out) 
      || (fd == 2 && thread_current() -> fd_std_err)
      || (fd == STDIN_FILENO && thread_current() -> fd_std_in))
  {
    result = false;
    goto done;
  }
  
  struct fd_table_element *fd_elem = find_file (fd);
  if (fd_elem == NULL)
    goto done;
  struct file * read = fd_elem -> file_name;
  if (read == NULL)
  {
      goto done;
  }
  struct inode *inode = file_get_inode(read);
  if(inode_isDir(inode))
    result = true;
  
  done:
   // lock_release(&file_lock);
  return result;
}

int inumber(int fd)
{
//  lock_acquire(&file_lock);
  int result = 0;
  if (fd < 0 || (fd == STDOUT_FILENO && thread_current() -> fd_std_out) 
      || (fd == 2 && thread_current() -> fd_std_err)
      || (fd == STDIN_FILENO && thread_current() -> fd_std_in))
  {
    result = -1;
    goto done;
  }
  
  struct fd_table_element *fd_elem = find_file (fd);
  if (fd_elem == NULL)
    goto done;
  struct file * read = fd_elem -> file_name;
  if (read == NULL)
  {
      result = -1;
      goto done;
  }
  struct inode *inode = file_get_inode(read);
  result = inode->sector;
  done:
  //  lock_release(&file_lock);
  return result;
}

static void
syscall_handler (struct intr_frame *f) 
{

   ASSERT(dir_isDir(thread_current()->cwd));
  
  uint32_t *sp = (uint32_t *) f->esp;
  thread_current() -> esp = f -> esp;
  bool sys_num_valid;
  int sys_num = getl_user(&sp[0], &sys_num_valid);
  if (!sys_num_valid)
    on_pgfault ();
  bool arg0_valid, arg1_valid, arg2_valid;
  int arg0 = getl_user (&sp[1], &arg0_valid);
  int arg1 = getl_user (&sp[2], &arg1_valid);
  int arg2 = getl_user (&sp[3], &arg2_valid);
  int result = 0;
//  printf ("arg: %s", arg1);
  switch (sys_num)
  {
    case SYS_HALT:
      halt ();
      break;
    case SYS_EXIT:
      if (!arg0_valid)
          on_pgfault();
      exit((int) arg0);
      break;
    case SYS_EXEC:
      if(!arg0_valid)
        on_pgfault();
      result = (int) exec((const char *) arg0);
      break;
    case SYS_WAIT:
      if (!arg0_valid)
        on_pgfault();
      result = wait((tid_t) arg0);
      break;
    case SYS_CREATE:
      if (!arg0_valid || !arg1_valid)
        on_pgfault();
      result = (int) create((const char*) arg0, (unsigned) arg1);
      break;
    case SYS_REMOVE:
      if (!arg0_valid)
        on_pgfault();
      result = (int) remove((const char *) arg0);
      break;
    case SYS_OPEN:
      if (!arg0_valid)
        on_pgfault();
      result = (int) open((const char *)arg0);
      break;
    case SYS_FILESIZE:
      if (!arg0_valid)
        on_pgfault();
      result = (int) filesize(arg0);
      break;
    case SYS_READ:
      if (!arg0_valid || !arg1_valid || !arg2_valid)
        on_pgfault();
      result = (int) read ((int) arg0, (void *) arg1, (unsigned) arg2);
      break;
    case SYS_WRITE:
      if (!arg0_valid || !arg1_valid || !arg2_valid)
        on_pgfault();
      result = write ((int) arg0, (const void *) arg1, (unsigned) arg2);
      break;
    case SYS_SEEK:
      if (!arg0_valid || !arg1_valid)
        on_pgfault();
      seek ((int) arg0, (unsigned) arg1);
      break;
    case SYS_TELL:
      if (!arg0_valid)
        on_pgfault();
      result = (int) tell ((int) arg0);
      break;
    case SYS_CLOSE:
      if (!arg0_valid)
        on_pgfault();
      close ((int) arg0);
      break;
    case SYS_MMAP:
      if (!arg0_valid || !arg1_valid)
        on_pgfault();
      result = mmap ((int) arg0, (void *) arg1);
      break;
    case SYS_MUNMAP:
      if (!arg0_valid)
        on_pgfault();
      munmap ((mapid_t) arg0);
      break;
    case SYS_CHDIR:
      if (!arg0_valid)
        on_pgfault();
      result = (int) chdir((const char *) arg0);
      break;
    case SYS_MKDIR:
      if (!arg0_valid)
        on_pgfault();
      result = (int) mkdir((const char *) arg0);
      break;
    case SYS_READDIR:
      if (!arg0_valid || !arg1_valid )
        on_pgfault();
      result = (int) readdir ((int) arg0, (char *) arg1);
      break;
    case SYS_ISDIR:
      if (!arg0_valid)
        on_pgfault();
      result = (int) isdir((int) arg0);
      break;
    case SYS_INUMBER:
      if (!arg0_valid)
        on_pgfault();
      result = (int) inumber((int) arg0);
      break;
    default:
      printf ("undefined system call(%d)!\n", sys_num);
      thread_exit (-1);
  }
  f->eax = result;
}
 	
void on_pgfault ()
{
	thread_exit(-1);
}

void halt ()
{
  power_off();
}

int wait (tid_t child_pid)
{
  return process_wait(child_pid);
}

void exit (int status)
{
	thread_exit(status);
}

tid_t exec (const char *file)
{
//  lock_acquire(&file_lock);
  tid_t result;
	bool valid = validate_buffer (file, "", PGSIZE, false);
  if (!valid)
  {
 //   lock_release(&file_lock);  
    //printf ("Exec: not valid %s \n");
    on_pgfault();
    NOT_REACHED();
  }
	result = process_exec (file);
//  lock_release(&file_lock);  
  return result;
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
getb_user (const uint32_t *uaddr, bool *is_valid)
{
  int result;
  *is_valid = false;
  /* Check if the user program tries to access
    kernel's virtual memory */
  if (is_user_vaddr(uaddr) == false) {
    result = -1;
  }
  else {
    asm ("movl $1f, %0; movl $0f, %%ebx;"
          "0:; movzbl %2, %0; movb $0x1, %1; 1:"
        : "=&a" (result), "=m" (*is_valid): "m" (*uaddr) : "ebx");
  }
  return result;
}
 
/* Reads a 32 bit long at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
getl_user (const uint32_t *uaddr, bool *is_valid)
{
  int result;
  *is_valid = false;
  /* Check if the user program tries to access
    kernel's virtual memory */
  if (is_user_vaddr(uaddr) == false) {
    result = -1;
  }
  else {
    asm ("movl $1f, %0; movl $0f, %%ebx;"
          "0:; movl %2, %0; movb $0x1, %1; 1:"
        : "=&a" (result), "=m" (*is_valid): "m" (*uaddr) : "ebx");
  }
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
putb_user (uint32_t *udst, uint8_t byte_val)
{
  bool is_valid = false;
  /* if the user program tries to access */
  /* kernel's virtual memory */
  if (is_user_vaddr(udst) == false) {
  	is_valid = false;
  }
  else {
	 asm ("movl $1f, %%eax; movl $0f, %%ebx;"
        "0:; movb %2, %1; movb $0x1, %0; 1:"
      : "=m" (is_valid), "=m" (*udst) : "q" (byte_val) : "eax", "ebx");
   }
  return is_valid;
}

/* Writes 32 bit long to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
putl_user (uint32_t *udst, uint32_t long_val)
{
  bool is_valid = false;
  /* if the user program tries to access */
  /* kernel's virtual memory */
  if (is_user_vaddr(udst) == false) {
  	is_valid = false;
  }
  else {
		 asm ("movl $1f, %%eax; movl $0f, %%ebx;"
	      "0:; movl %2, %1; movb $0x1, %0; 1:"
		   : "=m" (is_valid), "=m" (*udst) : "q" (long_val) : "eax", "ebx");
  }
  return is_valid;
}
