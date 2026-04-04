#ifndef RING_BUF_H
#define RING_BUF_H

#include <stdint.h>

#define RING_BUF_SIZE 64  // must be power of 2

typedef struct {
  uint32_t id;
  uint8_t  data[8];
  uint8_t  dlc;
} can_Frame;

typedef struct {
  can_Frame frames[RING_BUF_SIZE];
  volatile uint32_t head;  // written by producer (ISR)
  volatile uint32_t tail;  // read by consumer (main loop)
} ring_Buffer;

void ring_buf_init(ring_Buffer* rb);

// Push a frame. Returns 0 on success, -1 if full. Safe to call from ISR.
int ring_buf_push(ring_Buffer* rb, const can_Frame* frame);

// Pop a frame. Returns 0 on success, -1 if empty.
int ring_buf_pop(ring_Buffer* rb, can_Frame* frame);

int ring_buf_is_empty(const ring_Buffer* rb);
int ring_buf_is_full(const ring_Buffer* rb);
uint32_t ring_buf_count(const ring_Buffer* rb);

#endif // RING_BUF_H
