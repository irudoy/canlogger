#include "unity.h"
#include "mlvlg.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// --- mlg_swapend ---

void test_swapend_u16(void) {
  uint16_t val = 0x1234;
  uint8_t out[2];
  mlg_swapend(out, &val, 2);
  TEST_ASSERT_EQUAL_HEX8(0x12, out[0]); // big-endian: MSB first
  TEST_ASSERT_EQUAL_HEX8(0x34, out[1]);
}

void test_swapend_u32(void) {
  uint32_t val = 0xDEADBEEF;
  uint8_t out[4];
  mlg_swapend(out, &val, 4);
  TEST_ASSERT_EQUAL_HEX8(0xDE, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xAD, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0xBE, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0xEF, out[3]);
}

void test_swapend_float(void) {
  float val = 1.0f; // IEEE 754: 0x3F800000
  uint8_t out[4];
  mlg_swapend(out, &val, 4);
  TEST_ASSERT_EQUAL_HEX8(0x3F, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x80, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[3]);
}

void test_swapend_single_byte(void) {
  uint8_t val = 0xAB;
  uint8_t out;
  mlg_swapend(&out, &val, 1);
  TEST_ASSERT_EQUAL_HEX8(0xAB, out);
}

// --- mlg_field_data_size ---

void test_field_data_size(void) {
  TEST_ASSERT_EQUAL(1, mlg_field_data_size(MLG_U08));
  TEST_ASSERT_EQUAL(1, mlg_field_data_size(MLG_S08));
  TEST_ASSERT_EQUAL(2, mlg_field_data_size(MLG_U16));
  TEST_ASSERT_EQUAL(2, mlg_field_data_size(MLG_S16));
  TEST_ASSERT_EQUAL(4, mlg_field_data_size(MLG_U32));
  TEST_ASSERT_EQUAL(4, mlg_field_data_size(MLG_S32));
  TEST_ASSERT_EQUAL(4, mlg_field_data_size(MLG_F32));
  TEST_ASSERT_EQUAL(8, mlg_field_data_size(MLG_S64));
}

// --- mlg_write_header ---

void test_write_header_magic(void) {
  mlg_Header h = {
    .file_format = "MLVLG",
    .format_version = 2,
    .timestamp = 0x5F4D3C2B,
    .info_data_start = 0,
    .data_begin_index = 0x000000C0,
    .record_length = 2,
    .num_fields = 2
  };
  uint8_t buf[24];
  int ret = mlg_write_header(buf, sizeof(buf), &h);
  TEST_ASSERT_EQUAL(24, ret);

  // File format "MLVLG\0"
  TEST_ASSERT_EQUAL_HEX8(0x4D, buf[0]); // M
  TEST_ASSERT_EQUAL_HEX8(0x4C, buf[1]); // L
  TEST_ASSERT_EQUAL_HEX8(0x56, buf[2]); // V
  TEST_ASSERT_EQUAL_HEX8(0x4C, buf[3]); // L
  TEST_ASSERT_EQUAL_HEX8(0x47, buf[4]); // G
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]);

  // Version = 0x0002 BE
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[6]);
  TEST_ASSERT_EQUAL_HEX8(0x02, buf[7]);

  // Timestamp = 0x5F4D3C2B BE
  TEST_ASSERT_EQUAL_HEX8(0x5F, buf[8]);
  TEST_ASSERT_EQUAL_HEX8(0x4D, buf[9]);
  TEST_ASSERT_EQUAL_HEX8(0x3C, buf[10]);
  TEST_ASSERT_EQUAL_HEX8(0x2B, buf[11]);

  // Info data start = 0
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[12]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[13]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[14]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[15]);

  // Data begin index = 0xC0
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[16]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[17]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[18]);
  TEST_ASSERT_EQUAL_HEX8(0xC0, buf[19]);

  // Record length = 2
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[20]);
  TEST_ASSERT_EQUAL_HEX8(0x02, buf[21]);

  // Num fields = 2
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[22]);
  TEST_ASSERT_EQUAL_HEX8(0x02, buf[23]);
}

void test_write_header_buffer_too_small(void) {
  mlg_Header h = { .file_format = "MLVLG", .format_version = 2 };
  uint8_t buf[10];
  TEST_ASSERT_EQUAL(-1, mlg_write_header(buf, sizeof(buf), &h));
}

// --- mlg_write_field ---

void test_write_field_size_and_layout(void) {
  mlg_Field field = {
    .type = MLG_U08,
    .name = "RPM",
    .units = "rpm",
    .display_style = MLG_FLOAT,
    .scale = 1.0f,
    .transform = 0.0f,
    .digits = 0,
    .category = "Engine"
  };
  uint8_t buf[89];
  int ret = mlg_write_field(buf, sizeof(buf), &field);
  TEST_ASSERT_EQUAL(89, ret);

  // Type
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);

  // Name starts at offset 1, "RPM" + zeros
  TEST_ASSERT_EQUAL_UINT8('R', buf[1]);
  TEST_ASSERT_EQUAL_UINT8('P', buf[2]);
  TEST_ASSERT_EQUAL_UINT8('M', buf[3]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[4]);

  // Units at offset 35, "rpm" + zeros
  TEST_ASSERT_EQUAL_UINT8('r', buf[35]);
  TEST_ASSERT_EQUAL_UINT8('p', buf[36]);
  TEST_ASSERT_EQUAL_UINT8('m', buf[37]);

  // Display style at offset 45
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[45]);

  // Scale at offset 46: 1.0f = 0x3F800000 in IEEE 754, big-endian
  TEST_ASSERT_EQUAL_HEX8(0x3F, buf[46]);
  TEST_ASSERT_EQUAL_HEX8(0x80, buf[47]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[48]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[49]);

  // Transform at offset 50: 0.0f = 0x00000000
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[50]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[51]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[52]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[53]);

  // Digits at offset 54
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[54]);

  // Category at offset 55: "Engine"
  TEST_ASSERT_EQUAL_UINT8('E', buf[55]);
  TEST_ASSERT_EQUAL_UINT8('n', buf[56]);
}

void test_write_field_buffer_too_small(void) {
  mlg_Field field = { .type = MLG_U08, .name = "X" };
  uint8_t buf[50];
  TEST_ASSERT_EQUAL(-1, mlg_write_field(buf, sizeof(buf), &field));
}

void test_write_field_negative_digits(void) {
  mlg_Field field = {
    .type = MLG_F32, .name = "Temp", .units = "C",
    .scale = 0.1f, .transform = 0.0f, .digits = -1
  };
  uint8_t buf[89];
  mlg_write_field(buf, sizeof(buf), &field);
  // digits at offset 54, should preserve sign (-1 = 0xFF as uint8_t)
  TEST_ASSERT_EQUAL_HEX8(0xFF, buf[54]);
}

// --- mlg_write_data_block ---

void test_write_data_block_format(void) {
  uint8_t data[] = { 0x0A, 0x14 }; // two U08 field values
  uint8_t buf[32];
  int ret = mlg_write_data_block(buf, sizeof(buf), 0x03, 0x1234, data, 2);

  // Total = 1 + 1 + 2 + 2 + 1 = 7
  TEST_ASSERT_EQUAL(7, ret);

  TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]); // block type
  TEST_ASSERT_EQUAL_HEX8(0x03, buf[1]); // counter
  TEST_ASSERT_EQUAL_HEX8(0x12, buf[2]); // timestamp MSB
  TEST_ASSERT_EQUAL_HEX8(0x34, buf[3]); // timestamp LSB
  TEST_ASSERT_EQUAL_HEX8(0x0A, buf[4]); // data[0]
  TEST_ASSERT_EQUAL_HEX8(0x14, buf[5]); // data[1]
  TEST_ASSERT_EQUAL_HEX8(0x1E, buf[6]); // CRC: 0x0A + 0x14 = 0x1E
}

void test_write_data_block_crc_overflow(void) {
  uint8_t data[] = { 0xFF, 0x02 }; // sum = 0x101, crc = 0x01
  uint8_t buf[32];
  mlg_write_data_block(buf, sizeof(buf), 0, 0, data, 2);
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[6]); // overflow wraps
}

void test_write_data_block_buffer_too_small(void) {
  uint8_t data[] = { 0x01, 0x02, 0x03 };
  uint8_t buf[5]; // needs 8 (1+1+2+3+1)
  TEST_ASSERT_EQUAL(-1, mlg_write_data_block(buf, sizeof(buf), 0, 0, data, 3));
}

// --- mlg_write_marker ---

void test_write_marker_format(void) {
  uint8_t buf[54];
  int ret = mlg_write_marker(buf, sizeof(buf), 5, 0xABCD, "Test marker");
  TEST_ASSERT_EQUAL(54, ret);

  TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]); // marker type
  TEST_ASSERT_EQUAL_HEX8(0x05, buf[1]); // counter
  TEST_ASSERT_EQUAL_HEX8(0xAB, buf[2]); // timestamp MSB
  TEST_ASSERT_EQUAL_HEX8(0xCD, buf[3]); // timestamp LSB

  // Message at offset 4
  TEST_ASSERT_EQUAL_UINT8('T', buf[4]);
  TEST_ASSERT_EQUAL_UINT8('e', buf[5]);
  // Padded with zeros
  TEST_ASSERT_EQUAL_UINT8(0, buf[4 + 11]);
}

void test_write_marker_buffer_too_small(void) {
  uint8_t buf[30];
  TEST_ASSERT_EQUAL(-1, mlg_write_marker(buf, sizeof(buf), 0, 0, "test"));
}

// --- mlg_record_length ---

void test_record_length_multi_field(void) {
  mlg_Field fields[3] = {
    { .type = MLG_U08 },  // 1
    { .type = MLG_U16 },  // 2
    { .type = MLG_F32 },  // 4
  };
  TEST_ASSERT_EQUAL(7, mlg_record_length(fields, 3));
}

// --- Integration: full file roundtrip compatible with mlg-converter ---

void test_full_file_structure(void) {
  mlg_Field fields[2] = {
    { .type = MLG_U08, .name = "RPM", .units = "rpm",
      .display_style = MLG_FLOAT, .scale = 1.0f, .transform = 0.0f,
      .digits = 0, .category = "cat1" },
    { .type = MLG_U08, .name = "TPS", .units = "%",
      .display_style = MLG_FLOAT, .scale = 1.0f, .transform = 0.0f,
      .digits = 0, .category = "cat1" },
  };

  size_t rec_len = mlg_record_length(fields, 2);
  TEST_ASSERT_EQUAL(2, rec_len);

  // Header: data begins right after header + fields (no info data)
  uint32_t data_begin = MLG_HEADER_SIZE + 2 * MLG_FIELD_SIZE;

  mlg_Header header = {
    .file_format = "MLVLG",
    .format_version = 2,
    .timestamp = 0,
    .info_data_start = 0,
    .data_begin_index = data_begin,
    .record_length = (uint16_t)rec_len,
    .num_fields = 2
  };

  // Serialize header
  uint8_t file_buf[512];
  size_t off = 0;

  int ret = mlg_write_header(file_buf + off, sizeof(file_buf) - off, &header);
  TEST_ASSERT_EQUAL(24, ret);
  off += ret;

  // Serialize fields
  for (int i = 0; i < 2; i++) {
    ret = mlg_write_field(file_buf + off, sizeof(file_buf) - off, &fields[i]);
    TEST_ASSERT_EQUAL(89, ret);
    off += ret;
  }

  // Verify data_begin_index matches actual offset
  TEST_ASSERT_EQUAL(data_begin, off);

  // Serialize one data block
  uint8_t data[] = { 100, 50 };
  ret = mlg_write_data_block(file_buf + off, sizeof(file_buf) - off,
                              0, 0x0100, data, 2);
  TEST_ASSERT_EQUAL(7, ret);
  off += ret;

  // Verify total file size
  TEST_ASSERT_EQUAL(24 + 2*89 + 7, off); // 209 bytes
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_swapend_u16);
  RUN_TEST(test_swapend_u32);
  RUN_TEST(test_swapend_float);
  RUN_TEST(test_swapend_single_byte);
  RUN_TEST(test_field_data_size);
  RUN_TEST(test_write_header_magic);
  RUN_TEST(test_write_header_buffer_too_small);
  RUN_TEST(test_write_field_size_and_layout);
  RUN_TEST(test_write_field_buffer_too_small);
  RUN_TEST(test_write_field_negative_digits);
  RUN_TEST(test_write_data_block_format);
  RUN_TEST(test_write_data_block_crc_overflow);
  RUN_TEST(test_write_data_block_buffer_too_small);
  RUN_TEST(test_write_marker_format);
  RUN_TEST(test_write_marker_buffer_too_small);
  RUN_TEST(test_record_length_multi_field);
  RUN_TEST(test_full_file_structure);
  return UNITY_END();
}
