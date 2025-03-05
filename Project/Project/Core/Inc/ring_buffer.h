#ifndef INC_RING_BUFFER_H_
#define INC_RING_BUFFER_H_

#include <stdint.h>
#include <stddef.h>

typedef struct ring_buffer_ {
    uint8_t *buffer;
    uint16_t head;
    uint16_t tail;
    uint8_t is_full;
    uint16_t capacity;
} ring_buffer_t;

void ring_buffer_init(ring_buffer_t *rb, uint8_t *mem_add, uint16_t capacity);
void ring_buffer_reset(ring_buffer_t *rb);
uint16_t ring_buffer_size(ring_buffer_t *rb);
uint8_t ring_buffer_is_full(ring_buffer_t *rb);
uint8_t ring_buffer_is_empty(ring_buffer_t *rb);

void ring_buffer_write(ring_buffer_t *rb, uint8_t data);
uint8_t ring_buffer_read(ring_buffer_t *rb, uint8_t *byte);

size_t ring_buffer_peek(ring_buffer_t *rb, uint8_t *dest, size_t len);
uint8_t ring_buffer_find_command(ring_buffer_t *rb, const char *command, uint8_t cmd_length);

#endif /* INC_RING_BUFFER_H_ */