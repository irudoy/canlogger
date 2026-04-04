#include "unity.h"
#include "ring_buf.h"
#include <string.h>

static ring_Buffer rb;

void setUp(void) {
  ring_buf_init(&rb);
}
void tearDown(void) {}

// --- Init ---

void test_init_empty(void) {
  TEST_ASSERT_TRUE(ring_buf_is_empty(&rb));
  TEST_ASSERT_FALSE(ring_buf_is_full(&rb));
  TEST_ASSERT_EQUAL(0, ring_buf_count(&rb));
}

// --- Push/Pop ---

void test_push_pop_single(void) {
  can_Frame in = { .id = 0x123, .data = {1, 2, 3}, .dlc = 3 };
  can_Frame out = {0};

  TEST_ASSERT_EQUAL(0, ring_buf_push(&rb, &in));
  TEST_ASSERT_EQUAL(1, ring_buf_count(&rb));
  TEST_ASSERT_FALSE(ring_buf_is_empty(&rb));

  TEST_ASSERT_EQUAL(0, ring_buf_pop(&rb, &out));
  TEST_ASSERT_EQUAL_HEX32(0x123, out.id);
  TEST_ASSERT_EQUAL(3, out.dlc);
  TEST_ASSERT_EQUAL_UINT8(1, out.data[0]);
  TEST_ASSERT_EQUAL_UINT8(2, out.data[1]);
  TEST_ASSERT_EQUAL_UINT8(3, out.data[2]);
  TEST_ASSERT_TRUE(ring_buf_is_empty(&rb));
}

void test_pop_empty_returns_error(void) {
  can_Frame out;
  TEST_ASSERT_EQUAL(-1, ring_buf_pop(&rb, &out));
}

void test_fifo_order(void) {
  can_Frame f1 = { .id = 0x100 };
  can_Frame f2 = { .id = 0x200 };
  can_Frame f3 = { .id = 0x300 };
  can_Frame out;

  ring_buf_push(&rb, &f1);
  ring_buf_push(&rb, &f2);
  ring_buf_push(&rb, &f3);
  TEST_ASSERT_EQUAL(3, ring_buf_count(&rb));

  ring_buf_pop(&rb, &out);
  TEST_ASSERT_EQUAL_HEX32(0x100, out.id);
  ring_buf_pop(&rb, &out);
  TEST_ASSERT_EQUAL_HEX32(0x200, out.id);
  ring_buf_pop(&rb, &out);
  TEST_ASSERT_EQUAL_HEX32(0x300, out.id);
}

// --- Full buffer ---

void test_fill_to_capacity(void) {
  can_Frame f = { .id = 0 };
  // Can hold RING_BUF_SIZE - 1 elements
  for (int i = 0; i < RING_BUF_SIZE - 1; i++) {
    f.id = i;
    TEST_ASSERT_EQUAL(0, ring_buf_push(&rb, &f));
  }
  TEST_ASSERT_TRUE(ring_buf_is_full(&rb));
  TEST_ASSERT_EQUAL(RING_BUF_SIZE - 1, ring_buf_count(&rb));

  // One more push should fail
  TEST_ASSERT_EQUAL(-1, ring_buf_push(&rb, &f));
}

// --- Wraparound ---

void test_wraparound(void) {
  can_Frame f = { .id = 0 };
  can_Frame out;

  // Fill half, drain half, fill again to force wraparound
  for (int i = 0; i < RING_BUF_SIZE / 2; i++) {
    f.id = i;
    ring_buf_push(&rb, &f);
  }
  for (int i = 0; i < RING_BUF_SIZE / 2; i++) {
    ring_buf_pop(&rb, &out);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)i, out.id);
  }
  TEST_ASSERT_TRUE(ring_buf_is_empty(&rb));

  // Now push RING_BUF_SIZE - 1 elements (will wrap around)
  for (int i = 0; i < RING_BUF_SIZE - 1; i++) {
    f.id = 0x1000 + i;
    TEST_ASSERT_EQUAL(0, ring_buf_push(&rb, &f));
  }
  TEST_ASSERT_TRUE(ring_buf_is_full(&rb));

  // Pop all and verify order
  for (int i = 0; i < RING_BUF_SIZE - 1; i++) {
    ring_buf_pop(&rb, &out);
    TEST_ASSERT_EQUAL_HEX32(0x1000 + i, out.id);
  }
  TEST_ASSERT_TRUE(ring_buf_is_empty(&rb));
}

// --- Data integrity ---

void test_full_frame_data(void) {
  can_Frame in = {
    .id = 0x7FF,
    .data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
    .dlc = 8
  };
  can_Frame out = {0};

  ring_buf_push(&rb, &in);
  ring_buf_pop(&rb, &out);

  TEST_ASSERT_EQUAL_HEX32(0x7FF, out.id);
  TEST_ASSERT_EQUAL(8, out.dlc);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(in.data, out.data, 8);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_empty);
  RUN_TEST(test_push_pop_single);
  RUN_TEST(test_pop_empty_returns_error);
  RUN_TEST(test_fifo_order);
  RUN_TEST(test_fill_to_capacity);
  RUN_TEST(test_wraparound);
  RUN_TEST(test_full_frame_data);
  return UNITY_END();
}
