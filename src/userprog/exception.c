#include "userprog/exception.h"
//13018
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/vm.h"
#include "threads/palloc.h"
#include "vm/frame.h"
#include "threads/pte.h"
#include "lib/string.h"
/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);
// static bool handle_page_mapping_present(struct sup_page_table_entry *);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
/*      intr_dump_frame (f);*/
      thread_exit (-1);
      NOT_REACHED();
    case SEL_KCSEG:
/*    	intr_dump_frame (f);*/
      /* Check if page fault occured while running syscall_handler.
      	 put_user() and get_user() makes f->eax = f->eip to differentiate
      	 between kernel bug and unauthorized memory access in syscall.  */
      if((uint32_t) f->eip == (uint32_t) f->ebx) {
      	/* Set tf->eax to -1 so that put_user() and get_user() come to know
      	   that the memory access was unauthorized. */  
      	f->eip = (void *)f->eax;
      	f->eax = -1;
      }
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      else {
      	PANIC ("Kernel bug - unexpected interrupt in kernel"); 
      }
	  break;
    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
/*      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",*/
/*             f->vec_no, intr_name (f->vec_no), f->cs);*/
      thread_exit (-1);
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write UNUSED;        /* True: access was write, false: access was read. */
  bool user UNUSED;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;


  #ifdef DEBUG
  printf ("Debug user: %d\n", user);
  #endif

  /*
   * If the fault address is null, or the fault_address is present, or
   * the fault address is not user valid ( i.e. it s above PHYS_BASE)
   * then we can simple assert that we want the thread to die.
   * This behavior is currently not handled.
   */
  if (fault_addr == NULL || !not_present || !is_user_vaddr (fault_addr))
  {
    thread_exit (-1);
  }

  struct thread *t;
  struct sup_page_table_entry *sup_pt_entry;

  t = thread_current ();
  
  /* We are proceeding only if the fault_address is not present */
  ASSERT (not_present);

  sup_pt_entry = find_page_by_vaddr (pg_round_down (fault_addr));

  switch (f -> cs)
  {
    /*
     * User's code segment, thus this is an user exception, which is the
     * normal case
     */
    case (SEL_UCSEG):
      if (not_present)
      {
       /*
        * Check if it's the case of stack growth.
        * Checks if the stack growth is valid, if yes installs a page for
        * the stack and return else move forward. Limited stack to a size of
        * 8MB.
        */
        if (((uint32_t)f-> esp - 32 <= (uint32_t)fault_addr)  &&
        (uint32_t) f -> esp >= (uint32_t)(PHYS_BASE - STACK_MAX_PAGES*PGSIZE))
        {
          if (sup_pt_entry)
          {
            bool success = sup_page_table_load (sup_pt_entry);
            if (!success)
              thread_exit(-1);
            else
              return;
          }
          vm_create_page_and_alloc (pg_round_down(fault_addr));
          return;
        }
        
        /*
         * the page may be existing but may not be yet loaded
         * so load it now.
         */
        else if (sup_pt_entry != NULL)
        {
            bool success = sup_page_table_load (sup_pt_entry); 
            if (!success)
              thread_exit(-1);
            else
              return;
        }
        else
        {
          thread_exit (-1);
        }
      }
    break; 


    /* Kernel's code segment, we didn't expect to find bugs in the kernel.
     * Thus this must be an user space memory acess fault.
     * If this is due to page not present then we handle it in the same
     * way as we did in user's case.
     */ 
    case (SEL_KCSEG):

    /*
     * Checking if this was the bug from user or something wicked happened
     * with the kernel code itself.
     * If t -> esp is user valid, then we can be sure that this is user.
     * We are setting t -> esp in syscall handler.
     * This part is tricky. We've designed our code in such a way that the
     * entry point of user-kernel interaction is from syscall handler only.
     * Thus we can store the esp in syscall handler, and then check it here.
     */
    if (is_user_vaddr(t -> esp) && is_user_vaddr(fault_addr))
    {
      sup_pt_entry = find_page_by_vaddr (pg_round_down(fault_addr));
      /*
       * Handling Stack growth with the esp of user and not the esp from the
       * trap frame.
       */
      if (((uint32_t)t-> esp - 32 <= (uint32_t)fault_addr)  &&
        (uint32_t)t -> esp  >= (uint32_t)(PHYS_BASE - STACK_MAX_PAGES*PGSIZE))
      {
        if (sup_pt_entry)
        {
          bool success = sup_page_table_load (sup_pt_entry);
          if (!success)
            thread_exit(-1);
          else
            return;
        }
        vm_create_page_and_alloc (pg_round_down(fault_addr));
        return;
      }
      /*
       * Handling other cases similar to user's case above
       */
      if (sup_pt_entry != NULL)
      {
        bool success = sup_page_table_load (sup_pt_entry);
        if (!success)
          thread_exit(-1);
        else
          return;
     }
      else
      {
        thread_exit (-1);
      }
      NOT_REACHED();
    }
    
    break;
    default:
      PANIC ("Unknown bug \n");
  }

  kill (f);
}
