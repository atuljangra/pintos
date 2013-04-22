#ifndef USERPROG_SYSCALL_H
//32306
#define USERPROG_SYSCALL_H
#include <stdint.h>
#include <stdbool.h>
#include "threads/synch.h"
void syscall_init (void);
/* global file lock*/
struct lock file_lock;
#endif /* userprog/syscall.h */
