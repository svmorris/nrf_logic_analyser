#ifndef TH_RINGBUFFER_H
#define TH_RINGBUFFER_H

#include <stdint.h>

#define RINGBUFFER_SIZE 50000
extern int ringbuffer_index;
extern uint8_t ringbuffer[RINGBUFFER_SIZE];

uint8_t ring_buffer_clear();
int ring_buffer_write_one(uint8_t byte);
int ring_buffer_write(uint8_t *buf, int write_size);
int ring_buffer_read(uint8_t* retbuf, int read_size);

#endif
