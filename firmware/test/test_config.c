#include "unity.h"
#include "config.h"
#include <string.h>

static cfg_Config cfg;

void setUp(void) {
  memset(&cfg, 0, sizeof(cfg));
}
void tearDown(void) {}

// --- Basic parsing ---

void test_parse_minimal(void) {
  const char* ini =
    "[logger]\n"
    "interval_ms = 50\n"
    "\n"
    "[field]\n"
    "can_id = 0x123\n"
    "name = RPM\n"
    "units = rpm\n"
    "start_byte = 0\n"
    "bit_length = 8\n"
    "type = U08\n"
    "scale = 1.0\n"
    "offset = 0.0\n";

  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_OK, ret);
  TEST_ASSERT_EQUAL(50, cfg.log_interval_ms);
  TEST_ASSERT_EQUAL(1, cfg.num_fields);
  TEST_ASSERT_EQUAL_HEX32(0x123, cfg.fields[0].can_id);
  TEST_ASSERT_EQUAL_STRING("RPM", cfg.fields[0].name);
  TEST_ASSERT_EQUAL_STRING("rpm", cfg.fields[0].units);
  TEST_ASSERT_EQUAL(0, cfg.fields[0].start_byte);
  TEST_ASSERT_EQUAL(8, cfg.fields[0].bit_length);
  TEST_ASSERT_EQUAL(0, cfg.fields[0].type); // MLG_U08 = 0
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, cfg.fields[0].scale);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, cfg.fields[0].offset);
}

void test_parse_two_fields(void) {
  const char* ini =
    "[logger]\n"
    "interval_ms = 10\n"
    "\n"
    "[field]\n"
    "can_id = 0x666\n"
    "name = Coolant Temp\n"
    "units = C\n"
    "start_byte = 1\n"
    "bit_length = 8\n"
    "type = U08\n"
    "scale = 1.0\n"
    "offset = -40.0\n"
    "digits = 0\n"
    "category = Engine\n"
    "\n"
    "[field]\n"
    "can_id = 0x667\n"
    "name = RPM\n"
    "units = rpm\n"
    "start_byte = 1\n"
    "bit_length = 16\n"
    "type = U16\n"
    "scale = 12.5\n"
    "offset = 0.0\n"
    "digits = 0\n"
    "category = Engine\n";

  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_OK, ret);
  TEST_ASSERT_EQUAL(10, cfg.log_interval_ms);
  TEST_ASSERT_EQUAL(2, cfg.num_fields);

  TEST_ASSERT_EQUAL_HEX32(0x666, cfg.fields[0].can_id);
  TEST_ASSERT_EQUAL_STRING("Coolant Temp", cfg.fields[0].name);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -40.0f, cfg.fields[0].offset);
  TEST_ASSERT_EQUAL_STRING("Engine", cfg.fields[0].category);

  TEST_ASSERT_EQUAL_HEX32(0x667, cfg.fields[1].can_id);
  TEST_ASSERT_EQUAL(16, cfg.fields[1].bit_length);
  TEST_ASSERT_EQUAL(2, cfg.fields[1].type); // MLG_U16 = 2
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, cfg.fields[1].scale);
}

// --- Type parsing ---

void test_parse_all_types(void) {
  const char* types[] = {"U08", "S08", "U16", "S16", "U32", "S32", "S64", "F32"};
  int expected[] = {0, 1, 2, 3, 4, 5, 6, 7};

  for (int i = 0; i < 8; i++) {
    char ini[256];
    snprintf(ini, sizeof(ini),
      "[logger]\ninterval_ms = 10\n"
      "[field]\ncan_id = 0x100\nname = F\nunits = u\n"
      "start_byte = 0\nbit_length = 8\ntype = %s\n"
      "scale = 1.0\noffset = 0.0\n", types[i]);

    memset(&cfg, 0, sizeof(cfg));
    int ret = cfg_parse(ini, strlen(ini), &cfg);
    TEST_ASSERT_EQUAL_MESSAGE(CFG_OK, ret, types[i]);
    TEST_ASSERT_EQUAL_MESSAGE(expected[i], cfg.fields[0].type, types[i]);
  }
}

// --- Optional fields ---

void test_defaults(void) {
  const char* ini =
    "[logger]\n"
    "interval_ms = 10\n"
    "[field]\n"
    "can_id = 0x100\n"
    "name = Test\n"
    "units = x\n"
    "start_byte = 0\n"
    "bit_length = 8\n"
    "type = U08\n"
    "scale = 1.0\n"
    "offset = 0.0\n";

  cfg_parse(ini, strlen(ini), &cfg);

  TEST_ASSERT_EQUAL(0, cfg.fields[0].digits);
  TEST_ASSERT_EQUAL(0, cfg.fields[0].display_style);
  TEST_ASSERT_EQUAL(0, cfg.fields[0].is_big_endian);
  TEST_ASSERT_EQUAL_STRING("", cfg.fields[0].category);
}

// --- Comments and whitespace ---

void test_comments_and_blank_lines(void) {
  const char* ini =
    "# This is a comment\n"
    "[logger]\n"
    "interval_ms = 10\n"
    "\n"
    "# Another comment\n"
    "[field]\n"
    "can_id = 0x100\n"
    "name = Test\n"
    "units = x\n"
    "start_byte = 0\n"
    "bit_length = 8\n"
    "type = U08\n"
    "scale = 1.0\n"
    "offset = 0.0\n";

  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_OK, ret);
  TEST_ASSERT_EQUAL(1, cfg.num_fields);
}

// --- Error cases ---

void test_no_logger_section(void) {
  const char* ini =
    "[field]\n"
    "can_id = 0x100\n"
    "name = Test\n"
    "units = x\n"
    "start_byte = 0\n"
    "bit_length = 8\n"
    "type = U08\n"
    "scale = 1.0\n"
    "offset = 0.0\n";

  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_ERR_MISSING, ret);
}

void test_empty_input(void) {
  int ret = cfg_parse("", 0, &cfg);
  TEST_ASSERT_EQUAL(CFG_ERR_MISSING, ret);
}

void test_too_many_fields(void) {
  char ini[8192];
  int off = snprintf(ini, sizeof(ini), "[logger]\ninterval_ms = 10\n");
  for (int i = 0; i < CFG_MAX_FIELDS + 1; i++) {
    off += snprintf(ini + off, sizeof(ini) - off,
      "[field]\ncan_id = 0x%X\nname = F%d\nunits = u\n"
      "start_byte = 0\nbit_length = 8\ntype = U08\n"
      "scale = 1.0\noffset = 0.0\n", i, i);
  }
  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_ERR_OVERFLOW, ret);
}

// --- Hex parsing ---

void test_hex_can_id(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\n"
    "[field]\ncan_id = 0x7FF\nname = T\nunits = u\n"
    "start_byte = 0\nbit_length = 8\ntype = U08\n"
    "scale = 1.0\noffset = 0.0\n";

  cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL_HEX32(0x7FF, cfg.fields[0].can_id);
}

void test_decimal_can_id(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\n"
    "[field]\ncan_id = 1639\nname = T\nunits = u\n"
    "start_byte = 0\nbit_length = 8\ntype = U08\n"
    "scale = 1.0\noffset = 0.0\n";

  cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL_HEX32(0x667, cfg.fields[0].can_id);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_minimal);
  RUN_TEST(test_parse_two_fields);
  RUN_TEST(test_parse_all_types);
  RUN_TEST(test_defaults);
  RUN_TEST(test_comments_and_blank_lines);
  RUN_TEST(test_no_logger_section);
  RUN_TEST(test_empty_input);
  RUN_TEST(test_too_many_fields);
  RUN_TEST(test_hex_can_id);
  RUN_TEST(test_decimal_can_id);
  return UNITY_END();
}
