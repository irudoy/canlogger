#ifndef MLVLG_H
#define MLVLG_H

#include "main.h"
#include <stdint.h>
#include <stddef.h>

#define MLVLG_MAGIC_NUMBER "MLVLG\x00"
#define MLVLG_VERSION 0x0100

#define MAX_FIELD_NAME_LENGTH 34
#define MAX_FIELD_UNITS_LENGTH 10

#define MAX_LOG_FIELDS 64
#define BUFFER_SIZE 4096

typedef enum {
  U08,
  S08,
  U16,
  S16,
  U32,
  S32,
  S64,
  F32
} FieldType;

typedef struct {
  FieldType type;
  char name[MAX_FIELD_NAME_LENGTH];
  char units[MAX_FIELD_UNITS_LENGTH];
  uint8_t display_style;
  float scale;
  float transform;
  int8_t digits;
} LogField;

typedef struct {
  uint8_t buffer[BUFFER_SIZE];
  size_t buffer_pos;
  LogField fields[MAX_LOG_FIELDS];
  uint16_t num_fields;
  uint32_t record_length;
} BinaryLog;

void init_log_field(LogField* field, FieldType type, const char* name, const char* units, uint8_t display_style, float scale, float transform, int8_t digits);
size_t get_log_field_size(const LogField* field);
float transform_value(const LogField* field, float raw_value);

int init_binary_log(BinaryLog* log);
int add_log_field(BinaryLog* log, const LogField* field);
int write_log_header(BinaryLog* log);
int write_log_record(BinaryLog* log, const void* data);
void close_binary_log(BinaryLog* log);

#endif // MLVLG_H
