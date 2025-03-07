#include "ring_buffer.h"
#include <string.h>
#include "main.h"

// Inicializar el ring buffer
void ring_buffer_init(ring_buffer_t *rb, uint8_t *mem_add, uint16_t capacity) {
    rb->buffer = mem_add;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->is_full = 0;
}

// Resetear el buffer
void ring_buffer_reset(ring_buffer_t *rb) {
    rb->head = rb->tail = rb->is_full = 0;
}

// Verificar si el buffer está lleno
uint8_t ring_buffer_is_full(ring_buffer_t *rb) {
    return rb->is_full;
}

// Verificar si el buffer está vacío
uint8_t ring_buffer_is_empty(ring_buffer_t *rb) {
    return (!rb->is_full && (rb->head == rb->tail));
}

// Obtener el tamaño del buffer
uint16_t ring_buffer_size(ring_buffer_t *rb) {
    if (rb->is_full) return rb->capacity;
    return (rb->head >= rb->tail) ? (rb->head - rb->tail) : (rb->capacity + rb->head - rb->tail);
}

// Escribir datos en el buffer
void ring_buffer_write(ring_buffer_t *rb, uint8_t data) {
    rb->buffer[rb->head] = data;
    rb->head = (rb->head + 1) % rb->capacity;

    if (rb->is_full) {
        rb->tail = (rb->tail + 1) % rb->capacity;
    }
    
    rb->is_full = (rb->head == rb->tail);
}

// Leer datos del buffer
uint8_t ring_buffer_read(ring_buffer_t *rb, uint8_t *byte) {
    if (ring_buffer_is_empty(rb)) return 0;

    *byte = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->is_full = 0;

    return 1;
}

// Obtener los datos del buffer sin eliminarlos
size_t ring_buffer_peek(ring_buffer_t *rb, uint8_t *dest, size_t len) {
    size_t count = 0;
    size_t idx = rb->tail; // Comienza a leer desde la cola

    while ((count < len) && (idx != rb->head)) { // Mientras no se alcance la cabeza
        dest[count++] = rb->buffer[idx];        // Copia el dato al destino
        idx = (idx + 1) % rb->capacity;         // Avanza al siguiente índice circular
    }
    return count; // Devuelve el número de elementos copiados
}

// Buscar un comando en el buffer
uint8_t ring_buffer_find_command(ring_buffer_t *rb, const char *command, uint8_t cmd_length) {
    uint8_t buffer_copy[rb->capacity];
    size_t count = ring_buffer_peek(rb, buffer_copy, rb->capacity);

    for (size_t i = 0; i <= count - cmd_length; i++) {
        if (memcmp(&buffer_copy[i], command, cmd_length) == 0) {
            return 1; // Comando encontrado
        }
    }
    return 0; // Comando no encontrado
}


