#ifndef VM_CLOCK_H
#define VM_CLOCK_H
void evict_init (void);
struct frame* evict_frame (struct list *);
#endif
