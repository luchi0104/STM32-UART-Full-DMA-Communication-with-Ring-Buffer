/*
 * ring_buffer.h
 *
 *  Created on: 2026年10月24日
 *      Author: My_User
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>

#define RING_BUFFER_SIZE 256U

typedef struct
{
	uint8_t buffer[RING_BUFFER_SIZE];
	volatile uint16_t head;
	volatile uint16_t tail;

	volatile uint32_t overflow_count;
} RingBuffer_t;

void RingBuffer_Init(RingBuffer_t *rb);

int RingBuffer_Push(RingBuffer_t *rb, uint8_t data);

int RingBuffer_Pop(RingBuffer_t *rb, uint8_t *data);

uint16_t RingBuffer_Available(const RingBuffer_t *rb);

void RingBuffer_clear(RingBuffer_t *rb);

#endif /* RING_BUFFER_H */
