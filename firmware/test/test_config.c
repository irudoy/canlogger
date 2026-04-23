#include "unity.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

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
  static char ini[32768];
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

// --- Invalid values ---

void test_invalid_type_rejected(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\n"
    "[field]\ncan_id = 0x100\nname = T\nunits = u\n"
    "start_byte = 0\nbit_length = 8\ntype = BOGUS\n"
    "scale = 1.0\noffset = 0.0\n";
  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_ERR_VALUE, ret);
}

void test_zero_bit_length_rejected(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\n"
    "[field]\ncan_id = 0x100\nname = T\nunits = u\n"
    "start_byte = 0\nbit_length = 0\ntype = U08\n"
    "scale = 1.0\noffset = 0.0\n";
  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_ERR_VALUE, ret);
}

void test_start_byte_overflow_rejected(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\n"
    "[field]\ncan_id = 0x100\nname = T\nunits = u\n"
    "start_byte = 7\nbit_length = 16\ntype = U16\n"
    "scale = 1.0\noffset = 0.0\n";
  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_ERR_VALUE, ret);
}

void test_long_line_skipped(void) {
  // Build a config with a line > 256 chars
  char ini[1024];
  int off = snprintf(ini, sizeof(ini), "[logger]\ninterval_ms = 10\n[field]\ncan_id = 0x100\nname = ");
  for (int i = 0; i < 260; i++) ini[off++] = 'A'; // 260-char name value
  off += snprintf(ini + off, sizeof(ini) - off,
    "\nunits = u\nstart_byte = 0\nbit_length = 8\ntype = U08\nscale = 1.0\noffset = 0.0\n");
  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_OK, ret);
  // Name line was skipped, so name should be empty
  TEST_ASSERT_EQUAL_STRING("", cfg.fields[0].name);
}

// --- CAN bitrate and ID collection ---

void test_default_bitrate(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\n"
    "[field]\ncan_id = 0x100\nname = T\nunits = u\n"
    "start_byte = 0\nbit_length = 8\ntype = U08\nscale = 1.0\noffset = 0.0\n";
  cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(500000, cfg.can_bitrate);
}

void test_custom_bitrate(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\ncan_bitrate = 250000\n"
    "[field]\ncan_id = 0x100\nname = T\nunits = u\n"
    "start_byte = 0\nbit_length = 8\ntype = U08\nscale = 1.0\noffset = 0.0\n";
  cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(250000, cfg.can_bitrate);
}

void test_unique_can_ids_collected(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\n"
    "[field]\ncan_id = 0x666\nname = A\nunits = u\n"
    "start_byte = 0\nbit_length = 8\ntype = U08\nscale = 1.0\noffset = 0.0\n"
    "[field]\ncan_id = 0x667\nname = B\nunits = u\n"
    "start_byte = 0\nbit_length = 8\ntype = U08\nscale = 1.0\noffset = 0.0\n"
    "[field]\ncan_id = 0x666\nname = C\nunits = u\n"
    "start_byte = 1\nbit_length = 8\ntype = U08\nscale = 1.0\noffset = 0.0\n";
  cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(2, cfg.num_can_ids); // 0x666 and 0x667
  TEST_ASSERT_EQUAL_HEX32(0x666, cfg.can_ids[0]);
  TEST_ASSERT_EQUAL_HEX32(0x667, cfg.can_ids[1]);
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

// --- LUT parsing ---

void test_parse_lut_basic(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\n"
    "[field]\ncan_id = 0x640\nname = IAT\nunits = C\n"
    "start_byte = 0\nbit_length = 16\ntype = S16\n"
    "scale = 0.1\noffset = 0.0\n"
    "lut = 192:120, 570:80, 2807:20, 4860:-40\n";

  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_OK, ret);
  TEST_ASSERT_EQUAL(4, cfg.fields[0].lut_count);
  TEST_ASSERT_EQUAL(192, cfg.fields[0].lut[0].input);
  TEST_ASSERT_EQUAL(120, cfg.fields[0].lut[0].output);
  TEST_ASSERT_EQUAL(570, cfg.fields[0].lut[1].input);
  TEST_ASSERT_EQUAL(80, cfg.fields[0].lut[1].output);
  TEST_ASSERT_EQUAL(2807, cfg.fields[0].lut[2].input);
  TEST_ASSERT_EQUAL(20, cfg.fields[0].lut[2].output);
  TEST_ASSERT_EQUAL(4860, cfg.fields[0].lut[3].input);
  TEST_ASSERT_EQUAL(-40, cfg.fields[0].lut[3].output);
}

void test_parse_lut_two_points(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\n"
    "[field]\ncan_id = 0x640\nname = MAP\nunits = kPa\n"
    "start_byte = 2\nbit_length = 16\ntype = U16\n"
    "scale = 0.1\noffset = 0.0\n"
    "lut = 400:20, 4650:250\n";

  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_OK, ret);
  TEST_ASSERT_EQUAL(2, cfg.fields[0].lut_count);
  TEST_ASSERT_EQUAL(400, cfg.fields[0].lut[0].input);
  TEST_ASSERT_EQUAL(20, cfg.fields[0].lut[0].output);
  TEST_ASSERT_EQUAL(4650, cfg.fields[0].lut[1].input);
  TEST_ASSERT_EQUAL(250, cfg.fields[0].lut[1].output);
}

void test_parse_lut_single_point_rejected(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\n"
    "[field]\ncan_id = 0x640\nname = T\nunits = u\n"
    "start_byte = 0\nbit_length = 16\ntype = U16\n"
    "scale = 1.0\noffset = 0.0\n"
    "lut = 100:50\n";

  int ret = cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(CFG_ERR_VALUE, ret);
}

void test_no_lut_by_default(void) {
  const char* ini =
    "[logger]\ninterval_ms = 10\n"
    "[field]\ncan_id = 0x100\nname = T\nunits = u\n"
    "start_byte = 0\nbit_length = 8\ntype = U08\n"
    "scale = 1.0\noffset = 0.0\n";

  cfg_parse(ini, strlen(ini), &cfg);
  TEST_ASSERT_EQUAL(0, cfg.fields[0].lut_count);
}

// --- Cansult config ---

void test_parse_cansult_config(void) {
  FILE* f = fopen("config.ini", "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "config.ini not found — run from test/ dir");
  char buf[8192];
  size_t len = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[len] = '\0';

  int ret = cfg_parse(buf, len, &cfg);
  TEST_ASSERT_EQUAL(CFG_OK, ret);
  TEST_ASSERT_EQUAL(50, cfg.log_interval_ms);
  TEST_ASSERT_EQUAL(30, cfg.num_fields);

  // [0] State: 0x665
  TEST_ASSERT_EQUAL_HEX32(0x665, cfg.fields[0].can_id);
  TEST_ASSERT_EQUAL_STRING("State", cfg.fields[0].name);

  // [1] Battery: 0x666, byte 0, U08, scale 0.08
  TEST_ASSERT_EQUAL_HEX32(0x666, cfg.fields[1].can_id);
  TEST_ASSERT_EQUAL_STRING("Battery", cfg.fields[1].name);
  TEST_ASSERT_EQUAL_STRING("V", cfg.fields[1].units);
  TEST_ASSERT_EQUAL(0, cfg.fields[1].start_byte);
  TEST_ASSERT_EQUAL(0, cfg.fields[1].type); // U08
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.08f, cfg.fields[1].scale);

  // [2] Coolant: 0x666, byte 1, offset -50
  TEST_ASSERT_EQUAL_STRING("Coolant", cfg.fields[2].name);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -50.0f, cfg.fields[2].offset);

  // [9] Speed: 0x667, byte 0 (shifted by AF Alpha + AF Alpha SL)
  TEST_ASSERT_EQUAL_HEX32(0x667, cfg.fields[9].can_id);
  TEST_ASSERT_EQUAL_STRING("Speed", cfg.fields[9].name);

  // [10] RPM: 0x667, byte 1, U16, big-endian, scale 12.5
  TEST_ASSERT_EQUAL_STRING("RPM", cfg.fields[10].name);
  TEST_ASSERT_EQUAL(1, cfg.fields[10].start_byte);
  TEST_ASSERT_EQUAL(16, cfg.fields[10].bit_length);
  TEST_ASSERT_EQUAL(2, cfg.fields[10].type); // U16
  TEST_ASSERT_EQUAL(1, cfg.fields[10].is_big_endian);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.5f, cfg.fields[10].scale);

  // [13] Throttle Closed: 0x668, byte 0, bit 0, 1-bit field
  TEST_ASSERT_EQUAL_HEX32(0x668, cfg.fields[13].can_id);
  TEST_ASSERT_EQUAL_STRING("Throttle Closed", cfg.fields[13].name);
  TEST_ASSERT_EQUAL(0, cfg.fields[13].start_byte);
  TEST_ASSERT_EQUAL(0, cfg.fields[13].start_bit);
  TEST_ASSERT_EQUAL(1, cfg.fields[13].bit_length);

  // [16] VTC Solenoid: 0x668, byte 1, bit 5, 1-bit field
  TEST_ASSERT_EQUAL_STRING("VTC Solenoid", cfg.fields[16].name);
  TEST_ASSERT_EQUAL(1, cfg.fields[16].start_byte);
  TEST_ASSERT_EQUAL(5, cfg.fields[16].start_bit);
  TEST_ASSERT_EQUAL(1, cfg.fields[16].bit_length);

  // [26] Lambda: 0x180 (AEM 29-bit extended), byte 0, U16, scale 0.0001
  TEST_ASSERT_EQUAL_HEX32(0x180, cfg.fields[26].can_id);
  TEST_ASSERT_EQUAL_STRING("Lambda", cfg.fields[26].name);
  TEST_ASSERT_EQUAL(0, cfg.fields[26].start_byte);
  TEST_ASSERT_EQUAL(2, cfg.fields[26].type); // U16
  TEST_ASSERT_EQUAL(1, cfg.fields[26].is_big_endian);
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.0001f, cfg.fields[26].scale);

  // [29] SysVolts: 0x180, byte 4, U08, scale 0.1
  TEST_ASSERT_EQUAL_STRING("SysVolts", cfg.fields[29].name);
  TEST_ASSERT_EQUAL(0, cfg.fields[29].type); // U08
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, cfg.fields[29].scale);

  // 7 unique CAN IDs: 0x665, 0x666, 0x667, 0x668, 0x66B, 0x640, 0x180
  TEST_ASSERT_EQUAL(7, cfg.num_can_ids);
}

// --- Validation keys (valid_min / valid_max / invalid_strategy / preset) ---

void test_parse_valid_min_max(void) {
  const char* text =
    "[logger]\n"
    "interval_ms = 10\n"
    "[field]\n"
    "can_id = 0x100\n"
    "name = RPM\n"
    "units = rpm\n"
    "start_byte = 0\n"
    "bit_length = 16\n"
    "type = U16\n"
    "scale = 12.5\n"
    "valid_min = 0\n"
    "valid_max = 9000\n";
  TEST_ASSERT_EQUAL(CFG_OK, cfg_parse(text, strlen(text), &cfg));
  TEST_ASSERT_EQUAL(1, cfg.fields[0].has_valid_min);
  TEST_ASSERT_EQUAL(1, cfg.fields[0].has_valid_max);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, cfg.fields[0].valid_min);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 9000.0f, cfg.fields[0].valid_max);
  TEST_ASSERT_EQUAL(CFG_INVALID_LAST_GOOD, cfg.fields[0].invalid_strategy);
}

void test_parse_invalid_strategy_values(void) {
  const char* tpl =
    "[logger]\n"
    "interval_ms = 10\n"
    "[field]\n"
    "can_id = 0x100\n"
    "name = Test\n"
    "start_byte = 0\n"
    "bit_length = 8\n"
    "type = U08\n"
    "valid_max = 10\n"
    "invalid_strategy = %s\n";
  char buf[256];

  snprintf(buf, sizeof(buf), tpl, "last_good");
  memset(&cfg, 0, sizeof(cfg));
  TEST_ASSERT_EQUAL(CFG_OK, cfg_parse(buf, strlen(buf), &cfg));
  TEST_ASSERT_EQUAL(CFG_INVALID_LAST_GOOD, cfg.fields[0].invalid_strategy);

  snprintf(buf, sizeof(buf), tpl, "clamp");
  memset(&cfg, 0, sizeof(cfg));
  TEST_ASSERT_EQUAL(CFG_OK, cfg_parse(buf, strlen(buf), &cfg));
  TEST_ASSERT_EQUAL(CFG_INVALID_CLAMP, cfg.fields[0].invalid_strategy);

  snprintf(buf, sizeof(buf), tpl, "skip");
  memset(&cfg, 0, sizeof(cfg));
  TEST_ASSERT_EQUAL(CFG_OK, cfg_parse(buf, strlen(buf), &cfg));
  TEST_ASSERT_EQUAL(CFG_INVALID_SKIP, cfg.fields[0].invalid_strategy);
}

void test_parse_invalid_strategy_unknown_rejected(void) {
  const char* text =
    "[logger]\n"
    "interval_ms = 10\n"
    "[field]\n"
    "can_id = 0x100\n"
    "name = Test\n"
    "start_byte = 0\n"
    "bit_length = 8\n"
    "type = U08\n"
    "invalid_strategy = bogus\n";
  TEST_ASSERT_EQUAL(CFG_ERR_VALUE, cfg_parse(text, strlen(text), &cfg));
}

void test_parse_preset_aem_uego(void) {
  const char* text =
    "[logger]\n"
    "interval_ms = 10\n"
    "[field]\n"
    "can_id = 0x180\n"
    "is_extended = 1\n"
    "name = Lambda\n"
    "start_byte = 0\n"
    "bit_length = 16\n"
    "type = U16\n"
    "is_big_endian = 1\n"
    "preset = aem_uego\n";
  TEST_ASSERT_EQUAL(CFG_OK, cfg_parse(text, strlen(text), &cfg));
  TEST_ASSERT_EQUAL(CFG_PRESET_AEM_UEGO, cfg.fields[0].preset);
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
  RUN_TEST(test_default_bitrate);
  RUN_TEST(test_custom_bitrate);
  RUN_TEST(test_unique_can_ids_collected);
  RUN_TEST(test_invalid_type_rejected);
  RUN_TEST(test_zero_bit_length_rejected);
  RUN_TEST(test_start_byte_overflow_rejected);
  RUN_TEST(test_long_line_skipped);
  RUN_TEST(test_hex_can_id);
  RUN_TEST(test_decimal_can_id);
  RUN_TEST(test_parse_lut_basic);
  RUN_TEST(test_parse_lut_two_points);
  RUN_TEST(test_parse_lut_single_point_rejected);
  RUN_TEST(test_no_lut_by_default);
  RUN_TEST(test_parse_cansult_config);
  RUN_TEST(test_parse_valid_min_max);
  RUN_TEST(test_parse_invalid_strategy_values);
  RUN_TEST(test_parse_invalid_strategy_unknown_rejected);
  RUN_TEST(test_parse_preset_aem_uego);
  return UNITY_END();
}
