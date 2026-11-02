/*
 * ring_buffer.c
 *
 *  Created on: 2026年10月24日
 *      Author: My_User
 */
#include "ring_buffer.h"
#include <stddef.h>

void RingBuffer_Init(RingBuffer_t *rb)
{
	if(rb == NULL)
		return;

	rb->head = 0;
	rb->tail = 0;
	rb->overflow_count = 0;
}

int RingBuffer_Push(RingBuffer_t *rb, uint8_t data)
{
    if (rb == NULL)
        return 0;

    uint16_t next_head =
        (rb->head + 1U) % RING_BUFFER_SIZE;

    if (next_head == rb->tail)
    {
        rb->overflow_count++;
        return 0;
    }

    rb->buffer[rb->head] = data;
    rb->head = next_head;

    return 1;
}

int RingBuffer_Pop(RingBuffer_t *rb, uint8_t *data)
{
    if (rb == NULL || data == NULL)
        return 0;

    if (rb->head == rb->tail)
        return 0;

    *data = rb->buffer[rb->tail];

    rb->tail =
        (rb->tail + 1U) % RING_BUFFER_SIZE;

    return 1;
}

uint16_t RingBuffer_Available(const RingBuffer_t *rb)
{
    if (rb == NULL)
        return 0;

    return (rb->head + RING_BUFFER_SIZE - rb->tail)
           % RING_BUFFER_SIZE;
}

void RingBuffer_Clear(RingBuffer_t *rb)
{
    if (rb == NULL)
        return;

    rb->head = 0;
    rb->tail = 0;
    rb->overflow_count = 0;
}
