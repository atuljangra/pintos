#ifndef DEVICES_SERIAL_H
//15994
#define DEVICES_SERIAL_H

#include <stdint.h>

void serial_init_queue (void);
void serial_putc (uint8_t);
void serial_flush (void);
void serial_notify (void);

#endif /* devices/serial.h */
