#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_core.h>

#include "th_ringbuffer.h"

LOG_MODULE_REGISTER(TH_RINGBUFFER, LOG_LEVEL_INF);

int ringbuffer_index;
uint8_t ringbuffer[RINGBUFFER_SIZE];


uint8_t ring_buffer_clear()
{
    ringbuffer_index = 0;
    memset(ringbuffer, 0, RINGBUFFER_SIZE);
    return 0;
}


// returns the new index
int ring_buffer_write(uint8_t *buf, int write_size)
{
    if (write_size >= RINGBUFFER_SIZE)
        return -1;

    int i = 0;
    while (i < write_size)
    {
        ringbuffer[ringbuffer_index] = buf[i++];
        ringbuffer_index = (ringbuffer_index+1) % (RINGBUFFER_SIZE-1);
    }
    return ringbuffer_index;
}

int ring_buffer_write_one(uint8_t byte)
{
    ringbuffer[ringbuffer_index] = byte;
    ringbuffer_index = (ringbuffer_index+1) % (RINGBUFFER_SIZE-1);
    return ringbuffer_index;
}

int ring_buffer_read(uint8_t* retbuf, int read_size)
{
    if (read_size <= 0 || read_size > RINGBUFFER_SIZE)
        return -1;

    int i = 0;
    int ringpointer = ((ringbuffer_index - read_size) % RINGBUFFER_SIZE + RINGBUFFER_SIZE) % RINGBUFFER_SIZE;
    while (i < read_size)
    {
        retbuf[i++] = ringbuffer[ringpointer];
        ringpointer = (ringpointer + 1) % RINGBUFFER_SIZE;
    }
    return 0;
}
