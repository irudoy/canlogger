#include "unity.h"
#include "config.h"
#include "demo_gen.h"
#include "can_map.h"
#include "mlvlg.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

// --- Config parsing tests ---

static const char* MINI_DEMO_CONFIG =
  "[logger]\n"
  "interval_ms=10\n"
  "\n"
  "[field]\n"
  "name=RPM\n"
  "units=rpm\n"
  "type=U16\n"
  "scale=1\n"
  "demo_func=sine\n"
  "demo_min=800\n"
  "demo_max=8000\n"
  "demo_period_ms=4000\n"
  "\n"
  "[field]\n"
  "name=TPS\n"
  "units=%\n"
  "type=U08\n"
  "scale=1\n"
  "demo_func=ramp\n"
  "demo_min=0\n"
  "demo_max=100\n"
  "demo_period_ms=2000\n"
  "\n"
  "[field]\n"
  "name=CLT\n"
  "units=degC\n"
  "type=S16\n"
  "scale=0.1\n"
  "demo_func=noise\n"
  "demo_min=80\n"
  "demo_max=100\n"
  "demo_smoothing=0.95\n";

void test_parse_demo_config(void) {
  cfg_Config cfg;
  int rc = cfg_parse(MINI_DEMO_CONFIG, strlen(MINI_DEMO_CONFIG), &cfg);
  TEST_ASSERT_EQUAL(CFG_OK, rc);
  TEST_ASSERT_EQUAL(1, cfg.demo);
  TEST_ASSERT_EQUAL(3, cfg.num_fields);

  TEST_ASSERT_EQUAL(DEMO_SINE, cfg.demo_gen.params[0].func);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 800.0f, cfg.demo_gen.params[0].min_val);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 8000.0f, cfg.demo_gen.params[0].max_val);
  TEST_ASSERT_EQUAL(4000, cfg.demo_gen.params[0].period_ms);

  TEST_ASSERT_EQUAL(DEMO_RAMP, cfg.demo_gen.params[1].func);
  TEST_ASSERT_EQUAL(DEMO_NOISE, cfg.demo_gen.params[2].func);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.95f, cfg.demo_gen.params[2].smoothing);
}

void test_demo_no_can_validation(void) {
  // Demo fields shouldn't need can_id, start_byte etc.
  cfg_Config cfg;
  int rc = cfg_parse(MINI_DEMO_CONFIG, strlen(MINI_DEMO_CONFIG), &cfg);
  TEST_ASSERT_EQUAL(CFG_OK, rc);
  // can_id should be 0 (not set)
  TEST_ASSERT_EQUAL(0, cfg.fields[0].can_id);
}

void test_demo_gen_init(void) {
  cfg_Config cfg;
  cfg_parse(MINI_DEMO_CONFIG, strlen(MINI_DEMO_CONFIG), &cfg);

  // demo_init should have been called by cfg_parse
  TEST_ASSERT_EQUAL(1, cfg.demo_gen.enabled);
  TEST_ASSERT_EQUAL(3, cfg.demo_gen.num_fields);
  // Noise state should be initialized to midpoint
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 90.0f, cfg.demo_gen.fstate[2].state);
}

// --- Generation tests ---

void test_demo_sine_at_quarter_period(void) {
  cfg_Config cfg;
  cfg_parse(MINI_DEMO_CONFIG, strlen(MINI_DEMO_CONFIG), &cfg);

  can_FieldValues fv;
  can_map_init(&fv, &cfg);

  uint8_t types[] = { MLG_U16, MLG_U08, MLG_S16 };
  float scales[] = { 1.0f, 1.0f, 0.1f };
  float offsets[] = { 0.0f, 0.0f, 0.0f };

  // At t=1000ms (1/4 period for 4000ms sine), RPM should be at max (8000)
  demo_generate(&cfg.demo_gen, fv.values, fv.record_length,
                types, scales, offsets, 3, 1000);

  // Read RPM (U16 big-endian at offset 0)
  uint16_t rpm = (fv.values[0] << 8) | fv.values[1];
  // sine at quarter period = max = 8000
  TEST_ASSERT_INT_WITHIN(100, 8000, rpm);
}

void test_demo_ramp_midpoint(void) {
  cfg_Config cfg;
  cfg_parse(MINI_DEMO_CONFIG, strlen(MINI_DEMO_CONFIG), &cfg);

  can_FieldValues fv;
  can_map_init(&fv, &cfg);

  uint8_t types[] = { MLG_U16, MLG_U08, MLG_S16 };
  float scales[] = { 1.0f, 1.0f, 0.1f };
  float offsets[] = { 0.0f, 0.0f, 0.0f };

  // At t=1000ms (half of 2000ms ramp), TPS should be ~50
  demo_generate(&cfg.demo_gen, fv.values, fv.record_length,
                types, scales, offsets, 3, 1000);

  // TPS is U08 at offset 2 (after U16 RPM)
  uint8_t tps = fv.values[2];
  TEST_ASSERT_INT_WITHIN(2, 50, tps);
}

void test_demo_noise_stays_in_range(void) {
  cfg_Config cfg;
  cfg_parse(MINI_DEMO_CONFIG, strlen(MINI_DEMO_CONFIG), &cfg);

  can_FieldValues fv;
  can_map_init(&fv, &cfg);

  uint8_t types[] = { MLG_U16, MLG_U08, MLG_S16 };
  float scales[] = { 1.0f, 1.0f, 0.1f };
  float offsets[] = { 0.0f, 0.0f, 0.0f };

  // Run 1000 ticks, verify CLT stays in 80-100 range
  for (uint32_t t = 0; t < 1000; t++) {
    demo_generate(&cfg.demo_gen, fv.values, fv.record_length,
                  types, scales, offsets, 3, t * 10);

    // CLT is S16 at offset 3 (after U16 + U08), big-endian
    int16_t raw = (int16_t)((fv.values[3] << 8) | fv.values[4]);
    // display = (raw + offset) * scale = raw * 0.1
    float display = raw * 0.1f;
    TEST_ASSERT_TRUE_MESSAGE(display >= 79.0f && display <= 101.0f,
                             "CLT noise out of range");
  }
}

void test_demo_const_value(void) {
  const char* cfg_text =
    "[logger]\n"
    "interval_ms=10\n"
    "[field]\n"
    "name=Test\n"
    "units=V\n"
    "type=U16\n"
    "scale=0.01\n"
    "demo_func=const\n"
    "demo_min=13.5\n";

  cfg_Config cfg;
  cfg_parse(cfg_text, strlen(cfg_text), &cfg);

  can_FieldValues fv;
  can_map_init(&fv, &cfg);

  uint8_t types[] = { MLG_U16 };
  float scales[] = { 0.01f };
  float offsets[] = { 0.0f };

  demo_generate(&cfg.demo_gen, fv.values, fv.record_length,
                types, scales, offsets, 1, 5000);

  uint16_t raw = (fv.values[0] << 8) | fv.values[1];
  float display = raw * 0.01f;
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 13.5f, display);
}

void test_demo_square_wave(void) {
  const char* cfg_text =
    "[logger]\n"
    "interval_ms=10\n"
    "[field]\n"
    "name=Fan\n"
    "units=A\n"
    "type=U08\n"
    "scale=1\n"
    "demo_func=square\n"
    "demo_min=0\n"
    "demo_max=12\n"
    "demo_period_ms=1000\n";

  cfg_Config cfg;
  cfg_parse(cfg_text, strlen(cfg_text), &cfg);

  can_FieldValues fv;
  can_map_init(&fv, &cfg);

  uint8_t types[] = { MLG_U08 };
  float scales[] = { 1.0f };
  float offsets[] = { 0.0f };

  // First half of period -> max
  demo_generate(&cfg.demo_gen, fv.values, fv.record_length,
                types, scales, offsets, 1, 200);
  TEST_ASSERT_EQUAL(12, fv.values[0]);

  // Second half of period -> min
  demo_generate(&cfg.demo_gen, fv.values, fv.record_length,
                types, scales, offsets, 1, 700);
  TEST_ASSERT_EQUAL(0, fv.values[0]);
}

// --- Full demo config parse test ---

void test_parse_full_demo_config(void) {
  FILE* f = fopen("demo_config.ini", "r");
  if (!f) f = fopen("test/demo_config.ini", "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "demo_config.ini not found");

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buf = malloc(size + 1);
  fread(buf, 1, size, f);
  buf[size] = '\0';
  fclose(f);

  cfg_Config cfg;
  int rc = cfg_parse(buf, size, &cfg);
  free(buf);

  TEST_ASSERT_EQUAL_MESSAGE(CFG_OK, rc, "Failed to parse demo_config.ini");
  TEST_ASSERT_EQUAL(32, cfg.num_fields);
  TEST_ASSERT_EQUAL(1, cfg.demo);
  TEST_ASSERT_EQUAL(1, cfg.demo_gen.enabled);

  // Spot checks
  TEST_ASSERT_EQUAL_STRING("RPM", cfg.fields[0].name);
  TEST_ASSERT_EQUAL(DEMO_SINE, cfg.demo_gen.params[0].func);

  TEST_ASSERT_EQUAL_STRING("SteerAngle", cfg.fields[31].name);
  TEST_ASSERT_EQUAL(DEMO_SINE, cfg.demo_gen.params[31].func);

  TEST_ASSERT_EQUAL_STRING("Susp_FL", cfg.fields[19].name);
  TEST_ASSERT_EQUAL(DEMO_NOISE, cfg.demo_gen.params[19].func);
}

void test_parse_func_all_types(void) {
  TEST_ASSERT_EQUAL(DEMO_SINE, demo_parse_func("sine"));
  TEST_ASSERT_EQUAL(DEMO_RAMP, demo_parse_func("ramp"));
  TEST_ASSERT_EQUAL(DEMO_SQUARE, demo_parse_func("square"));
  TEST_ASSERT_EQUAL(DEMO_NOISE, demo_parse_func("noise"));
  TEST_ASSERT_EQUAL(DEMO_CONST, demo_parse_func("const"));
  TEST_ASSERT_EQUAL(DEMO_NONE, demo_parse_func("unknown"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_demo_config);
  RUN_TEST(test_demo_no_can_validation);
  RUN_TEST(test_demo_gen_init);
  RUN_TEST(test_demo_sine_at_quarter_period);
  RUN_TEST(test_demo_ramp_midpoint);
  RUN_TEST(test_demo_noise_stays_in_range);
  RUN_TEST(test_demo_const_value);
  RUN_TEST(test_demo_square_wave);
  RUN_TEST(test_parse_full_demo_config);
  RUN_TEST(test_parse_func_all_types);
  return UNITY_END();
}
