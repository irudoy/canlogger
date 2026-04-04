#include "ring_buf.h"
#include <string.h>

void ring_buf_init(ring_Buffer* rb) {
  rb->head = 0;
  rb->tail = 0;
}

int ring_buf_push(ring_Buffer* rb, const can_Frame* frame) {
  uint32_t next = (rb->head + 1) & (RING_BUF_SIZE - 1);
  if (next == rb->tail) {
    return -1; // full
  }
  rb->frames[rb->head] = *frame;
  rb->head = next;
  return 0;
}

int ring_buf_pop(ring_Buffer* rb, can_Frame* frame) {
  if (rb->head == rb->tail) {
    return -1; // empty
  }
  *frame = rb->frames[rb->tail];
  rb->tail = (rb->tail + 1) & (RING_BUF_SIZE - 1);
  return 0;
}

int ring_buf_is_empty(const ring_Buffer* rb) {
  return rb->head == rb->tail;
}

int ring_buf_is_full(const ring_Buffer* rb) {
  return ((rb->head + 1) & (RING_BUF_SIZE - 1)) == rb->tail;
}

uint32_t ring_buf_count(const ring_Buffer* rb) {
  return (rb->head - rb->tail) & (RING_BUF_SIZE - 1);
}
