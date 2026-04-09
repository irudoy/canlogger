#include "unity.h"
#include "demo_can.h"
#include "can_map.h"
#include "mlvlg.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

// --- Shared config (too large for stack at 256 fields) ---
static cfg_Config g_cfg;

static void parse_cfg(const char* text) {
  int rc = cfg_parse(text, strlen(text), &g_cfg);
  TEST_ASSERT_EQUAL_MESSAGE(CFG_OK, rc, "cfg_parse failed");
}

// --- Helper: read U16 big-endian from CAN data ---
static uint16_t read_u16_be(const uint8_t* data, uint8_t offset) {
  return (uint16_t)((data[offset] << 8) | data[offset + 1]);
}

// --- Helper: read U16 little-endian from CAN data ---
static uint16_t read_u16_le(const uint8_t* data, uint8_t offset) {
  return (uint16_t)((data[offset + 1] << 8) | data[offset]);
}

// ============================================================
// 1. Pack single U08 field
// ============================================================
void test_pack_single_u08(void) {
  const char* text =
    "[logger]\ninterval_ms=1\n"
    "[field]\nname=T\nunits=V\ntype=U08\nscale=1\n"
    "can_id=0x100\nstart_byte=3\nbit_length=8\nis_big_endian=1\n"
    "demo_func=const\ndemo_min=42\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;
  TEST_ASSERT_EQUAL(1, cfg->demo_gen.use_ring_buf);

  ring_Buffer rb;
  ring_buf_init(&rb);

  int n = demo_pack_can_frames(cfg, &rb, 0);
  TEST_ASSERT_EQUAL(1, n);

  can_Frame frame;
  TEST_ASSERT_EQUAL(0, ring_buf_pop(&rb, &frame));
  TEST_ASSERT_EQUAL(0x100, frame.id);
  TEST_ASSERT_EQUAL(42, frame.data[3]);
}

// ============================================================
// 2. Pack U16 big-endian
// ============================================================
void test_pack_u16_big_endian(void) {
  const char* text =
    "[logger]\ninterval_ms=1\n"
    "[field]\nname=T\nunits=rpm\ntype=U16\nscale=1\n"
    "can_id=0x200\nstart_byte=0\nbit_length=16\nis_big_endian=1\n"
    "demo_func=const\ndemo_min=1000\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;
  ring_Buffer rb;
  ring_buf_init(&rb);

  demo_pack_can_frames(cfg, &rb, 0);

  can_Frame frame;
  ring_buf_pop(&rb, &frame);
  uint16_t val = read_u16_be(frame.data, 0);
  TEST_ASSERT_EQUAL(1000, val);
}

// ============================================================
// 3. Pack U16 little-endian
// ============================================================
void test_pack_u16_little_endian(void) {
  const char* text =
    "[logger]\ninterval_ms=1\n"
    "[field]\nname=T\nunits=rpm\ntype=U16\nscale=1\n"
    "can_id=0x200\nstart_byte=0\nbit_length=16\nis_big_endian=0\n"
    "demo_func=const\ndemo_min=1000\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;
  ring_Buffer rb;
  ring_buf_init(&rb);

  demo_pack_can_frames(cfg, &rb, 0);

  can_Frame frame;
  ring_buf_pop(&rb, &frame);
  uint16_t val = read_u16_le(frame.data, 0);
  TEST_ASSERT_EQUAL(1000, val);
}

// ============================================================
// 4. Multiple fields in same CAN ID
// ============================================================
void test_pack_multiple_fields_same_id(void) {
  const char* text =
    "[logger]\ninterval_ms=1\n"
    "[field]\nname=A\nunits=x\ntype=U16\nscale=1\n"
    "can_id=0x300\nstart_byte=0\nbit_length=16\nis_big_endian=1\n"
    "demo_func=const\ndemo_min=100\n"
    "[field]\nname=B\nunits=x\ntype=U16\nscale=1\n"
    "can_id=0x300\nstart_byte=2\nbit_length=16\nis_big_endian=1\n"
    "demo_func=const\ndemo_min=200\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;
  ring_Buffer rb;
  ring_buf_init(&rb);

  int n = demo_pack_can_frames(cfg, &rb, 0);
  TEST_ASSERT_EQUAL(1, n); // single frame for single CAN ID

  can_Frame frame;
  ring_buf_pop(&rb, &frame);
  TEST_ASSERT_EQUAL(100, read_u16_be(frame.data, 0));
  TEST_ASSERT_EQUAL(200, read_u16_be(frame.data, 2));
}

// ============================================================
// 5. Multiple CAN IDs → multiple frames
// ============================================================
void test_pack_multiple_can_ids(void) {
  const char* text =
    "[logger]\ninterval_ms=1\n"
    "[field]\nname=A\nunits=x\ntype=U16\nscale=1\n"
    "can_id=0x400\nstart_byte=0\nbit_length=16\nis_big_endian=1\n"
    "demo_func=const\ndemo_min=111\n"
    "[field]\nname=B\nunits=x\ntype=U16\nscale=1\n"
    "can_id=0x401\nstart_byte=0\nbit_length=16\nis_big_endian=1\n"
    "demo_func=const\ndemo_min=222\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;
  ring_Buffer rb;
  ring_buf_init(&rb);

  int n = demo_pack_can_frames(cfg, &rb, 0);
  TEST_ASSERT_EQUAL(2, n);

  can_Frame f1, f2;
  ring_buf_pop(&rb, &f1);
  ring_buf_pop(&rb, &f2);
  TEST_ASSERT_EQUAL(0x400, f1.id);
  TEST_ASSERT_EQUAL(111, read_u16_be(f1.data, 0));
  TEST_ASSERT_EQUAL(0x401, f2.id);
  TEST_ASSERT_EQUAL(222, read_u16_be(f2.data, 0));
}

// ============================================================
// 6. Roundtrip: pack → can_map_process → verify display value
// ============================================================
void test_roundtrip_u16(void) {
  const char* text =
    "[logger]\ninterval_ms=1\n"
    "[field]\nname=RPM\nunits=rpm\ntype=U16\nscale=1\n"
    "can_id=0x500\nstart_byte=0\nbit_length=16\nis_big_endian=1\n"
    "demo_func=const\ndemo_min=4400\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;

  // Pack into CAN frame
  ring_Buffer rb;
  ring_buf_init(&rb);
  demo_pack_can_frames(cfg, &rb, 0);

  can_Frame frame;
  ring_buf_pop(&rb, &frame);

  // Process through can_map (the real unpack path)
  can_FieldValues fv;
  can_map_init(&fv, cfg);
  can_map_process(&fv, cfg, &frame);

  // Read display value: raw * scale + offset (scale=1, offset=0)
  uint16_t raw = (fv.values[0] << 8) | fv.values[1];
  TEST_ASSERT_EQUAL(4400, raw);
}

// ============================================================
// 7. Roundtrip with non-trivial scale/offset
// ============================================================
void test_roundtrip_with_scale_offset(void) {
  // display = (raw + offset) * scale
  // e.g. Temperature: display = raw * 0.1, so scale=0.1, offset=0
  // demo_min=85.0 → raw = 85.0 / 0.1 = 850
  const char* text =
    "[logger]\ninterval_ms=1\n"
    "[field]\nname=CLT\nunits=C\ntype=U16\nscale=0.1\n"
    "can_id=0x600\nstart_byte=0\nbit_length=16\nis_big_endian=1\n"
    "demo_func=const\ndemo_min=85.0\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;
  ring_Buffer rb;
  ring_buf_init(&rb);
  demo_pack_can_frames(cfg, &rb, 0);

  can_Frame frame;
  ring_buf_pop(&rb, &frame);

  can_FieldValues fv;
  can_map_init(&fv, cfg);
  can_map_process(&fv, cfg, &frame);

  uint16_t raw = (fv.values[0] << 8) | fv.values[1];
  float display = raw * 0.1f;
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 85.0f, display);
}

// ============================================================
// 8. Roundtrip S32 (but packing only supports up to U16 in CAN;
//    test with U08 field that goes negative via S08)
// ============================================================
void test_roundtrip_s08(void) {
  const char* text =
    "[logger]\ninterval_ms=1\n"
    "[field]\nname=Temp\nunits=C\ntype=S08\nscale=1\n"
    "can_id=0x700\nstart_byte=0\nbit_length=8\nis_big_endian=1\n"
    "demo_func=const\ndemo_min=-20\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;
  ring_Buffer rb;
  ring_buf_init(&rb);
  demo_pack_can_frames(cfg, &rb, 0);

  can_Frame frame;
  ring_buf_pop(&rb, &frame);

  // S08: -20 → 0xEC (two's complement)
  TEST_ASSERT_EQUAL((uint8_t)(int8_t)-20, frame.data[0]);
}

// ============================================================
// 9. Inverse LUT accuracy
// ============================================================
void test_lut_inverse(void) {
  // LUT: 0:0, 100:50, 200:100 (linear)
  // display=75 → should be raw=150
  const char* text =
    "[logger]\ninterval_ms=1\n"
    "[field]\nname=Sens\nunits=kPa\ntype=U16\nscale=1\n"
    "can_id=0x800\nstart_byte=0\nbit_length=16\nis_big_endian=1\n"
    "lut=0:0, 100:50, 200:100\n"
    "demo_func=const\ndemo_min=75\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;
  ring_Buffer rb;
  ring_buf_init(&rb);
  demo_pack_can_frames(cfg, &rb, 0);

  can_Frame frame;
  ring_buf_pop(&rb, &frame);

  // Raw value in CAN should be ~150
  uint16_t raw = read_u16_be(frame.data, 0);
  TEST_ASSERT_INT_WITHIN(1, 150, raw);

  // Roundtrip through can_map_process (which applies forward LUT)
  can_FieldValues fv;
  can_map_init(&fv, cfg);
  can_map_process(&fv, cfg, &frame);

  // can_map applies LUT: raw=150 → display should be 75
  // Read the MLG raw value (after LUT → write_value)
  uint16_t mlg_raw = (fv.values[0] << 8) | fv.values[1];
  float display = mlg_raw * 1.0f; // scale=1, offset=0
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 75.0f, display);
}

// ============================================================
// 10. Ring buffer overflow returns -1
// ============================================================
void test_ring_buf_overflow(void) {
  const char* text =
    "[logger]\ninterval_ms=1\n"
    "[field]\nname=T\nunits=x\ntype=U08\nscale=1\n"
    "can_id=0x900\nstart_byte=0\nbit_length=8\nis_big_endian=1\n"
    "demo_func=const\ndemo_min=1\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;
  ring_Buffer rb;
  ring_buf_init(&rb);

  // Fill the ring buffer completely
  can_Frame dummy = {0};
  for (int i = 0; i < RING_BUF_SIZE - 1; i++) {
    ring_buf_push(&rb, &dummy);
  }

  // Now it should fail
  int n = demo_pack_can_frames(cfg, &rb, 0);
  TEST_ASSERT_EQUAL(-1, n);
}

// ============================================================
// 11. Stress test: 128 U16 fields on 32 CAN IDs
// ============================================================
void test_stress_128_u16(void) {
  FILE* f = fopen("demo_stress_128u16.ini", "r");
  if (!f) f = fopen("test/demo_stress_128u16.ini", "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "demo_stress_128u16.ini not found");

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buf = malloc(size + 1);
  fread(buf, 1, size, f);
  buf[size] = '\0';
  fclose(f);

  static cfg_Config cfg;
  int rc = cfg_parse(buf, size, &cfg);
  free(buf);

  TEST_ASSERT_EQUAL(CFG_OK, rc);
  TEST_ASSERT_EQUAL(128, cfg.num_fields);
  TEST_ASSERT_EQUAL(32, cfg.num_can_ids);
  TEST_ASSERT_EQUAL(1, cfg.demo_gen.use_ring_buf);

  ring_Buffer rb;
  ring_buf_init(&rb);

  // Pack at t=500ms
  int n = demo_pack_can_frames(&cfg, &rb, 500);
  TEST_ASSERT_EQUAL(32, n); // 32 CAN IDs → 32 frames

  // Drain all frames through can_map_process
  can_FieldValues fv;
  can_map_init(&fv, &cfg);

  can_Frame frame;
  int total = 0;
  while (ring_buf_pop(&rb, &frame) == 0) {
    can_map_process(&fv, &cfg, &frame);
    total++;
  }
  TEST_ASSERT_EQUAL(32, total);

  // Verify all 128 fields have values in 0-65000 range
  size_t offset = 0;
  for (int i = 0; i < 128; i++) {
    uint16_t val = (fv.values[offset] << 8) | fv.values[offset + 1];
    // At t=500ms with various waveforms, values should be in range
    TEST_ASSERT_TRUE_MESSAGE(val <= 65000, "Value out of range");
    offset += 2;
  }
}

// ============================================================
// 12. Noise: smooth transitions, different fields differ
// ============================================================
void test_noise_realistic(void) {
  const char* text =
    "[logger]\ninterval_ms=1\n"
    "[field]\nname=A\nunits=x\ntype=U16\nscale=0.01\n"
    "can_id=0xA00\nstart_byte=0\nbit_length=16\nis_big_endian=1\n"
    "demo_func=noise\ndemo_min=0\ndemo_max=100\ndemo_period_ms=5000\n"
    "[field]\nname=B\nunits=x\ntype=U16\nscale=0.01\n"
    "can_id=0xA00\nstart_byte=2\nbit_length=16\nis_big_endian=1\n"
    "demo_func=noise\ndemo_min=0\ndemo_max=100\ndemo_period_ms=5000\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;

  // Collect values over time
  float vals_a[100], vals_b[100];
  for (int t = 0; t < 100; t++) {
    ring_Buffer rb;
    ring_buf_init(&rb);
    // Reset last_pack_tick to allow generation each tick
    cfg->demo_gen.last_pack_tick = t * 50;
    demo_pack_can_frames(cfg, &rb, t * 50 + 1);

    can_Frame frame;
    ring_buf_pop(&rb, &frame);

    uint16_t raw_a = read_u16_be(frame.data, 0);
    uint16_t raw_b = read_u16_be(frame.data, 2);
    vals_a[t] = raw_a * 0.01f;
    vals_b[t] = raw_b * 0.01f;
  }

  // Check smoothness: consecutive values shouldn't jump more than 20% of range
  int smooth_count = 0;
  for (int t = 1; t < 100; t++) {
    float delta = fabsf(vals_a[t] - vals_a[t - 1]);
    if (delta < 20.0f) smooth_count++;
  }
  TEST_ASSERT_TRUE_MESSAGE(smooth_count > 90, "Noise signal not smooth enough");

  // Check that fields A and B are different (different random phases)
  int differ_count = 0;
  for (int t = 0; t < 100; t++) {
    if (fabsf(vals_a[t] - vals_b[t]) > 0.5f) differ_count++;
  }
  TEST_ASSERT_TRUE_MESSAGE(differ_count > 30, "Noise fields should differ");
}

// ============================================================
// 13. Rate limiting: no frames generated within interval
// ============================================================
void test_rate_limiting(void) {
  const char* text =
    "[logger]\ninterval_ms=10\n"
    "[field]\nname=T\nunits=x\ntype=U08\nscale=1\n"
    "can_id=0xB00\nstart_byte=0\nbit_length=8\nis_big_endian=1\n"
    "demo_func=const\ndemo_min=99\n";

  parse_cfg(text);
  cfg_Config* cfg = &g_cfg;
  ring_Buffer rb;
  ring_buf_init(&rb);

  // First call generates
  int n1 = demo_pack_can_frames(cfg, &rb, 0);
  TEST_ASSERT_EQUAL(1, n1);

  // Call 5ms later — should be rate-limited
  int n2 = demo_pack_can_frames(cfg, &rb, 5);
  TEST_ASSERT_EQUAL(0, n2);

  // Call 10ms later — should generate again
  int n3 = demo_pack_can_frames(cfg, &rb, 10);
  TEST_ASSERT_EQUAL(1, n3);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_pack_single_u08);
  RUN_TEST(test_pack_u16_big_endian);
  RUN_TEST(test_pack_u16_little_endian);
  RUN_TEST(test_pack_multiple_fields_same_id);
  RUN_TEST(test_pack_multiple_can_ids);
  RUN_TEST(test_roundtrip_u16);
  RUN_TEST(test_roundtrip_with_scale_offset);
  RUN_TEST(test_roundtrip_s08);
  RUN_TEST(test_lut_inverse);
  RUN_TEST(test_ring_buf_overflow);
  RUN_TEST(test_stress_128_u16);
  RUN_TEST(test_noise_realistic);
  RUN_TEST(test_rate_limiting);
  return UNITY_END();
}
