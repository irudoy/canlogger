#include "unity.h"
#include "mlvlg.h"
#include "config.h"
#include "can_map.h"
#include "ring_buf.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

// PoC test file: 2 fields (Counter U08, Sine U16), 20 data blocks
// This generates a complete .mlg file and writes it to snapshots/poc_test.mlg
// Then compares with etalon if it exists

#define NUM_FIELDS 2
#define NUM_BLOCKS 20
#define FILE_BUF_SIZE 4096

static uint8_t file_buf[FILE_BUF_SIZE];
static size_t file_size;

static int build_poc_mlg(void) {
  size_t off = 0;
  int ret;

  // Define fields
  mlg_Field fields[NUM_FIELDS] = {
    {
      .type = MLG_U08,
      .name = "Counter",
      .units = "count",
      .display_style = MLG_FLOAT,
      .scale = 1.0f,
      .transform = 0.0f,
      .digits = 0,
      .category = "Test"
    },
    {
      .type = MLG_U16,
      .name = "Sine",
      .units = "raw",
      .display_style = MLG_FLOAT,
      .scale = 1.0f,
      .transform = 0.0f,
      .digits = 0,
      .category = "Test"
    },
  };

  size_t rec_len = mlg_record_length(fields, NUM_FIELDS); // 1 + 2 = 3
  uint32_t data_begin = MLG_HEADER_SIZE + NUM_FIELDS * MLG_FIELD_SIZE;

  // Header
  mlg_Header header = {
    .file_format = "MLVLG",
    .format_version = 2,
    .timestamp = 1700000000, // fixed timestamp for reproducibility
    .info_data_start = 0,
    .data_begin_index = data_begin,
    .record_length = (uint16_t)rec_len,
    .num_fields = NUM_FIELDS
  };

  ret = mlg_write_header(file_buf + off, FILE_BUF_SIZE - off, &header);
  if (ret < 0) return -1;
  off += ret;

  // Field descriptors
  for (int i = 0; i < NUM_FIELDS; i++) {
    ret = mlg_write_field(file_buf + off, FILE_BUF_SIZE - off, &fields[i]);
    if (ret < 0) return -1;
    off += ret;
  }

  // Data blocks
  for (int i = 0; i < NUM_BLOCKS; i++) {
    uint8_t counter_val = (uint8_t)i;
    // Sine: 500 + 500*sin(i * 2*pi/20), range 0-1000, as U16 big-endian
    double angle = (double)i * 2.0 * 3.14159265358979 / NUM_BLOCKS;
    uint16_t sine_val = (uint16_t)(500.0 + 500.0 * sin(angle));

    uint8_t data[3]; // U08 + U16 = 3 bytes
    data[0] = counter_val;
    // U16 big-endian
    data[1] = (sine_val >> 8) & 0xFF;
    data[2] = sine_val & 0xFF;

    uint16_t timestamp = (uint16_t)(i * 1000); // 10ms intervals in 10us units

    ret = mlg_write_data_block(file_buf + off, FILE_BUF_SIZE - off,
                                (uint8_t)(i % 256), timestamp, data, rec_len);
    if (ret < 0) return -1;
    off += ret;
  }

  file_size = off;
  return 0;
}

void test_poc_mlg_builds_successfully(void) {
  TEST_ASSERT_EQUAL(0, build_poc_mlg());

  // Expected size: header(24) + 2*field(178) + 20*block(20 * (1+1+2+3+1)) = 24 + 178 + 160 = 362
  size_t expected = MLG_HEADER_SIZE + NUM_FIELDS * MLG_FIELD_SIZE + NUM_BLOCKS * (4 + 3 + 1);
  TEST_ASSERT_EQUAL(expected, file_size);
}

void test_poc_mlg_header_valid(void) {
  build_poc_mlg();

  // Magic
  TEST_ASSERT_EQUAL_UINT8('M', file_buf[0]);
  TEST_ASSERT_EQUAL_UINT8('L', file_buf[1]);
  TEST_ASSERT_EQUAL_UINT8('V', file_buf[2]);
  TEST_ASSERT_EQUAL_UINT8('L', file_buf[3]);
  TEST_ASSERT_EQUAL_UINT8('G', file_buf[4]);

  // Version = 2
  TEST_ASSERT_EQUAL_HEX8(0x00, file_buf[6]);
  TEST_ASSERT_EQUAL_HEX8(0x02, file_buf[7]);

  // Data begin index = 24 + 2*89 = 202 = 0xCA
  TEST_ASSERT_EQUAL_HEX8(0x00, file_buf[16]);
  TEST_ASSERT_EQUAL_HEX8(0x00, file_buf[17]);
  TEST_ASSERT_EQUAL_HEX8(0x00, file_buf[18]);
  TEST_ASSERT_EQUAL_HEX8(0xCA, file_buf[19]);

  // Record length = 3
  TEST_ASSERT_EQUAL_HEX8(0x00, file_buf[20]);
  TEST_ASSERT_EQUAL_HEX8(0x03, file_buf[21]);

  // Num fields = 2
  TEST_ASSERT_EQUAL_HEX8(0x00, file_buf[22]);
  TEST_ASSERT_EQUAL_HEX8(0x02, file_buf[23]);
}

void test_poc_mlg_first_data_block(void) {
  build_poc_mlg();

  uint32_t data_begin = MLG_HEADER_SIZE + NUM_FIELDS * MLG_FIELD_SIZE; // 202
  uint8_t* block = file_buf + data_begin;

  // Block type = 0
  TEST_ASSERT_EQUAL_HEX8(0x00, block[0]);
  // Counter (rolling) = 0
  TEST_ASSERT_EQUAL_HEX8(0x00, block[1]);
  // Timestamp = 0
  TEST_ASSERT_EQUAL_HEX8(0x00, block[2]);
  TEST_ASSERT_EQUAL_HEX8(0x00, block[3]);
  // Counter field = 0
  TEST_ASSERT_EQUAL_HEX8(0x00, block[4]);
  // Sine field: sin(0) = 0, so 500 + 0 = 500 = 0x01F4
  TEST_ASSERT_EQUAL_HEX8(0x01, block[5]);
  TEST_ASSERT_EQUAL_HEX8(0xF4, block[6]);
  // CRC: 0x00 + 0x01 + 0xF4 = 0xF5
  TEST_ASSERT_EQUAL_HEX8(0xF5, block[7]);
}

void test_poc_mlg_write_and_snapshot(void) {
  build_poc_mlg();

  // Write to file for external validation
  FILE* f = fopen("snapshots/poc_test.mlg", "wb");
  TEST_ASSERT_NOT_NULL(f);
  size_t written = fwrite(file_buf, 1, file_size, f);
  fclose(f);
  TEST_ASSERT_EQUAL(file_size, written);

  // Try to compare with etalon
  FILE* etalon = fopen("snapshots/poc_test.mlg.expected", "rb");
  if (etalon) {
    uint8_t etalon_buf[FILE_BUF_SIZE];
    size_t etalon_size = fread(etalon_buf, 1, FILE_BUF_SIZE, etalon);
    fclose(etalon);

    TEST_ASSERT_EQUAL_MESSAGE(etalon_size, file_size, "Snapshot size mismatch");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(etalon_buf, file_buf, file_size,
      "Snapshot content mismatch — run 'cp snapshots/poc_test.mlg snapshots/poc_test.mlg.expected' to update");
  } else {
    // First run — no etalon yet, just print instruction
    printf("  [INFO] No etalon found. Validate snapshots/poc_test.mlg manually, then:\n");
    printf("         cp snapshots/poc_test.mlg snapshots/poc_test.mlg.expected\n");
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_poc_mlg_builds_successfully);
  RUN_TEST(test_poc_mlg_header_valid);
  RUN_TEST(test_poc_mlg_first_data_block);
  RUN_TEST(test_poc_mlg_write_and_snapshot);
  return UNITY_END();
}
