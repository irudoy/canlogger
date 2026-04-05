#include "unity.h"
#include "can_map.h"
#include <string.h>

static cfg_Config cfg;
static can_FieldValues fv;

void setUp(void) {
  memset(&cfg, 0, sizeof(cfg));
  memset(&fv, 0, sizeof(fv));
}
void tearDown(void) {}

static void setup_single_u08_field(uint32_t can_id, uint8_t start_byte) {
  cfg.log_interval_ms = 10;
  cfg.num_fields = 1;
  cfg.fields[0].can_id = can_id;
  cfg.fields[0].start_byte = start_byte;
  cfg.fields[0].bit_length = 8;
  cfg.fields[0].type = 0; // MLG_U08
  cfg.fields[0].scale = 1.0f;
  cfg.fields[0].offset = 0.0f;
  strcpy(cfg.fields[0].name, "Test");
  strcpy(cfg.fields[0].units, "x");
}

// --- Init ---

void test_init_single_field(void) {
  setup_single_u08_field(0x123, 0);
  TEST_ASSERT_EQUAL(0, can_map_init(&fv, &cfg));
  TEST_ASSERT_EQUAL(1, fv.record_length);
  TEST_ASSERT_EQUAL(1, fv.num_fields);
  TEST_ASSERT_EQUAL(0, fv.updated);
}

void test_init_multi_field(void) {
  cfg.num_fields = 3;
  cfg.fields[0].type = 0; // U08 = 1
  cfg.fields[1].type = 2; // U16 = 2
  cfg.fields[2].type = 7; // F32 = 4
  TEST_ASSERT_EQUAL(0, can_map_init(&fv, &cfg));
  TEST_ASSERT_EQUAL(7, fv.record_length); // 1+2+4
}

// --- Process U08 ---

void test_process_u08(void) {
  setup_single_u08_field(0x666, 1);
  can_map_init(&fv, &cfg);

  can_Frame frame = { .id = 0x666, .data = {0xAA, 0xBB, 0xCC}, .dlc = 3 };
  int updated = can_map_process(&fv, &cfg, &frame);

  TEST_ASSERT_EQUAL(1, updated);
  TEST_ASSERT_EQUAL(1, fv.updated);
  TEST_ASSERT_EQUAL_HEX8(0xBB, fv.values[0]); // start_byte=1
}

void test_process_wrong_id_ignored(void) {
  setup_single_u08_field(0x666, 0);
  can_map_init(&fv, &cfg);

  can_Frame frame = { .id = 0x999, .data = {0xFF}, .dlc = 1 };
  int updated = can_map_process(&fv, &cfg, &frame);

  TEST_ASSERT_EQUAL(0, updated);
  TEST_ASSERT_EQUAL(0, fv.updated);
  TEST_ASSERT_EQUAL_HEX8(0x00, fv.values[0]); // unchanged
}

// --- Process U16 ---

void test_process_u16_little_endian(void) {
  cfg.num_fields = 1;
  cfg.fields[0].can_id = 0x667;
  cfg.fields[0].start_byte = 1;
  cfg.fields[0].bit_length = 16;
  cfg.fields[0].type = 2; // U16
  cfg.fields[0].is_big_endian = 0; // CAN data is LE
  can_map_init(&fv, &cfg);

  // CAN data LE: low=0x34, high=0x12 → value 0x1234
  can_Frame frame = { .id = 0x667, .data = {0x00, 0x34, 0x12}, .dlc = 3 };
  can_map_process(&fv, &cfg, &frame);

  // Output should be BE: 0x12, 0x34
  TEST_ASSERT_EQUAL_HEX8(0x12, fv.values[0]);
  TEST_ASSERT_EQUAL_HEX8(0x34, fv.values[1]);
}

void test_process_u16_big_endian(void) {
  cfg.num_fields = 1;
  cfg.fields[0].can_id = 0x667;
  cfg.fields[0].start_byte = 1;
  cfg.fields[0].bit_length = 16;
  cfg.fields[0].type = 2; // U16
  cfg.fields[0].is_big_endian = 1; // CAN data is BE
  can_map_init(&fv, &cfg);

  // CAN data BE: high=0x12, low=0x34
  can_Frame frame = { .id = 0x667, .data = {0x00, 0x12, 0x34}, .dlc = 3 };
  can_map_process(&fv, &cfg, &frame);

  // Output BE: 0x12, 0x34 (no swap)
  TEST_ASSERT_EQUAL_HEX8(0x12, fv.values[0]);
  TEST_ASSERT_EQUAL_HEX8(0x34, fv.values[1]);
}

// --- Multiple fields from same CAN ID ---

void test_two_fields_same_id(void) {
  cfg.num_fields = 2;
  cfg.fields[0] = (cfg_Field){ .can_id = 0x666, .start_byte = 0, .bit_length = 8, .type = 0 };
  cfg.fields[1] = (cfg_Field){ .can_id = 0x666, .start_byte = 1, .bit_length = 8, .type = 0 };
  can_map_init(&fv, &cfg);

  can_Frame frame = { .id = 0x666, .data = {0xAA, 0xBB}, .dlc = 2 };
  int updated = can_map_process(&fv, &cfg, &frame);

  TEST_ASSERT_EQUAL(2, updated);
  TEST_ASSERT_EQUAL_HEX8(0xAA, fv.values[0]);
  TEST_ASSERT_EQUAL_HEX8(0xBB, fv.values[1]);
}

// --- Multiple fields from different CAN IDs ---

void test_two_fields_different_ids(void) {
  cfg.num_fields = 2;
  cfg.fields[0] = (cfg_Field){ .can_id = 0x666, .start_byte = 0, .bit_length = 8, .type = 0 };
  cfg.fields[1] = (cfg_Field){ .can_id = 0x667, .start_byte = 0, .bit_length = 16, .type = 2, .is_big_endian = 1 };
  can_map_init(&fv, &cfg);

  // First frame updates field 0
  can_Frame f1 = { .id = 0x666, .data = {0xAA}, .dlc = 1 };
  can_map_process(&fv, &cfg, &f1);
  TEST_ASSERT_EQUAL_HEX8(0xAA, fv.values[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, fv.values[1]); // field 1 not yet updated

  // Second frame updates field 1
  can_Frame f2 = { .id = 0x667, .data = {0x12, 0x34}, .dlc = 2 };
  can_map_process(&fv, &cfg, &f2);
  TEST_ASSERT_EQUAL_HEX8(0xAA, fv.values[0]); // field 0 unchanged
  TEST_ASSERT_EQUAL_HEX8(0x12, fv.values[1]); // field 1 BE
  TEST_ASSERT_EQUAL_HEX8(0x34, fv.values[2]);
}

// --- build_mlg_fields ---

void test_build_mlg_fields(void) {
  cfg.num_fields = 1;
  strcpy(cfg.fields[0].name, "RPM");
  strcpy(cfg.fields[0].units, "rpm");
  strcpy(cfg.fields[0].category, "Engine");
  cfg.fields[0].type = 2;
  cfg.fields[0].scale = 12.5f;
  cfg.fields[0].offset = -40.0f;
  cfg.fields[0].digits = 1;

  mlg_Field mlg_fields[1];
  can_map_build_mlg_fields(&cfg, mlg_fields, 1);

  TEST_ASSERT_EQUAL(2, mlg_fields[0].type);
  TEST_ASSERT_EQUAL_STRING("RPM", mlg_fields[0].name);
  TEST_ASSERT_EQUAL_STRING("rpm", mlg_fields[0].units);
  TEST_ASSERT_EQUAL_STRING("Engine", mlg_fields[0].category);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, mlg_fields[0].scale);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -40.0f, mlg_fields[0].transform);
  TEST_ASSERT_EQUAL(1, mlg_fields[0].digits);
}

// --- Out-of-bounds extraction ---

void test_process_out_of_bounds_ignored(void) {
  cfg.num_fields = 1;
  cfg.fields[0] = (cfg_Field){
    .can_id = 0x100, .start_byte = 7, .bit_length = 16, .type = 2 // U16 at byte 7 = overflow
  };
  can_map_init(&fv, &cfg);

  can_Frame frame = { .id = 0x100, .data = {0}, .dlc = 8 };
  int updated = can_map_process(&fv, &cfg, &frame);
  TEST_ASSERT_EQUAL(0, updated);
  TEST_ASSERT_EQUAL_HEX8(0x00, fv.values[0]);
}

void test_process_beyond_dlc_ignored(void) {
  cfg.num_fields = 1;
  cfg.fields[0] = (cfg_Field){
    .can_id = 0x100, .start_byte = 3, .bit_length = 8, .type = 0 // U08 at byte 3
  };
  can_map_init(&fv, &cfg);

  // Frame with DLC=2 — only data[0] and data[1] are valid
  can_Frame frame = { .id = 0x100, .data = {0xAA, 0xBB, 0xCC, 0xDD}, .dlc = 2 };
  int updated = can_map_process(&fv, &cfg, &frame);
  TEST_ASSERT_EQUAL(0, updated); // byte 3 beyond DLC=2
}

// --- LUT interpolation ---

void test_lut_exact_point(void) {
  cfg.num_fields = 1;
  cfg.fields[0] = (cfg_Field){
    .can_id = 0x640, .start_byte = 0, .bit_length = 16,
    .type = 3, // S16
    .scale = 0.1f, .offset = 0.0f,
    .lut_count = 2,
    .lut = { {400, 20}, {4650, 250} }
  };
  can_map_init(&fv, &cfg);

  // Send 400 mV (LE: 0x90, 0x01)
  can_Frame frame = { .id = 0x640, .data = {0x90, 0x01}, .dlc = 2 };
  can_map_process(&fv, &cfg, &frame);

  // LUT: 400 → 20 kPa, stored = 20/0.1 = 200 = 0x00C8 BE
  TEST_ASSERT_EQUAL_HEX8(0x00, fv.values[0]);
  TEST_ASSERT_EQUAL_HEX8(0xC8, fv.values[1]);
}

void test_lut_interpolation_midpoint(void) {
  cfg.num_fields = 1;
  cfg.fields[0] = (cfg_Field){
    .can_id = 0x640, .start_byte = 0, .bit_length = 16,
    .type = 3, // S16
    .scale = 0.1f, .offset = 0.0f,
    .lut_count = 2,
    .lut = { {400, 20}, {4650, 250} }
  };
  can_map_init(&fv, &cfg);

  // Send 2525 mV (midpoint) → 135 kPa, stored = 1350 = 0x0546
  // 2525 LE = 0xDD, 0x09
  can_Frame frame = { .id = 0x640, .data = {0xDD, 0x09}, .dlc = 2 };
  can_map_process(&fv, &cfg, &frame);

  int16_t stored = (int16_t)((fv.values[0] << 8) | fv.values[1]);
  float display = stored * 0.1f;
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 135.0f, display);
}

void test_lut_clamp_below(void) {
  cfg.num_fields = 1;
  cfg.fields[0] = (cfg_Field){
    .can_id = 0x640, .start_byte = 0, .bit_length = 16,
    .type = 3, .scale = 0.1f, .offset = 0.0f,
    .lut_count = 2,
    .lut = { {400, 20}, {4650, 250} }
  };
  can_map_init(&fv, &cfg);

  // Send 100 mV (below LUT range) → clamp to 20 kPa
  can_Frame frame = { .id = 0x640, .data = {0x64, 0x00}, .dlc = 2 };
  can_map_process(&fv, &cfg, &frame);

  int16_t stored = (int16_t)((fv.values[0] << 8) | fv.values[1]);
  float display = stored * 0.1f;
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, display);
}

void test_lut_ntc_negative_output(void) {
  cfg.num_fields = 1;
  cfg.fields[0] = (cfg_Field){
    .can_id = 0x640, .start_byte = 0, .bit_length = 16,
    .type = 3, // S16
    .scale = 0.1f, .offset = 0.0f,
    .lut_count = 3,
    .lut = { {2807, 20}, {3848, 0}, {4860, -40} }
  };
  can_map_init(&fv, &cfg);

  // Send 3848 mV (= 0°C), stored = 0/0.1 = 0
  // 3848 LE = 0x08, 0x0F
  can_Frame frame = { .id = 0x640, .data = {0x08, 0x0F}, .dlc = 2 };
  can_map_process(&fv, &cfg, &frame);

  int16_t stored = (int16_t)((fv.values[0] << 8) | fv.values[1]);
  TEST_ASSERT_EQUAL(0, stored); // 0°C
}

void test_lut_no_lut_passthrough(void) {
  // Without LUT, raw value should pass through unchanged
  cfg.num_fields = 1;
  cfg.fields[0] = (cfg_Field){
    .can_id = 0x640, .start_byte = 0, .bit_length = 16,
    .type = 2, // U16
    .is_big_endian = 0,
    .lut_count = 0
  };
  can_map_init(&fv, &cfg);

  // Send 0x1234 LE
  can_Frame frame = { .id = 0x640, .data = {0x34, 0x12}, .dlc = 2 };
  can_map_process(&fv, &cfg, &frame);

  // Raw BE passthrough
  TEST_ASSERT_EQUAL_HEX8(0x12, fv.values[0]);
  TEST_ASSERT_EQUAL_HEX8(0x34, fv.values[1]);
}

// --- reset_updated ---

void test_reset_updated(void) {
  setup_single_u08_field(0x100, 0);
  can_map_init(&fv, &cfg);

  can_Frame frame = { .id = 0x100, .data = {0x42}, .dlc = 1 };
  can_map_process(&fv, &cfg, &frame);
  TEST_ASSERT_EQUAL(1, fv.updated);

  can_map_reset_updated(&fv);
  TEST_ASSERT_EQUAL(0, fv.updated);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_single_field);
  RUN_TEST(test_init_multi_field);
  RUN_TEST(test_process_u08);
  RUN_TEST(test_process_wrong_id_ignored);
  RUN_TEST(test_process_u16_little_endian);
  RUN_TEST(test_process_u16_big_endian);
  RUN_TEST(test_two_fields_same_id);
  RUN_TEST(test_two_fields_different_ids);
  RUN_TEST(test_process_out_of_bounds_ignored);
  RUN_TEST(test_process_beyond_dlc_ignored);
  RUN_TEST(test_build_mlg_fields);
  RUN_TEST(test_lut_exact_point);
  RUN_TEST(test_lut_interpolation_midpoint);
  RUN_TEST(test_lut_clamp_below);
  RUN_TEST(test_lut_ntc_negative_output);
  RUN_TEST(test_lut_no_lut_passthrough);
  RUN_TEST(test_reset_updated);
  return UNITY_END();
}
