#ifndef USERPROG_PROCESS_H
//11360
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
tid_t process_exec (const char* file_name);
int process_wait (tid_t child_tid);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
