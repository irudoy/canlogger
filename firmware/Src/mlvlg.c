#include "mlvlg.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#define MLG_FIELD_SIZE 89
#define MLG_FIELD_NAME_SIZE 34
#define MLG_FIELD_UNITS_SIZE 10
#define MLG_FIELD_CATEGORY_SIZE 34

static void memcpy_swapend(void* dest, const void* src, size_t num) {
  const uint8_t* src2 = (const uint8_t*)src;
  uint8_t* dest2 = (uint8_t*)dest;

  for (size_t i = 0; i < num; i++) {
    // Endian swap - copy the end to the beginning
    dest2[i] = src2[num - 1 - i];
  }
}

typedef enum {
  MLG_U08 = 0,
  MLG_S08 = 1,
  MLG_U16 = 2,
  MLG_S16 = 3,
  MLG_U32 = 4,
  MLG_S32 = 5,
  MLG_S64 = 6,
  MLG_F32 = 7
} mlg_FieldType;

typedef enum {
  MLG_FLOAT = 0,
  MLG_HEX = 1,
  MLG_BITS = 2,
  MLG_DATE = 3,
  MLG_ON_OFF = 4,
  MLG_YES_NO = 5,
  MLG_HIGH_LOW = 6,
  MLG_ACTIVE_INACTIVE = 7,
  MLG_TRUE_FALSE = 8
} mlg_DisplayStyle;

typedef struct {
  uint8_t type;
  uint8_t name[MLG_FIELD_NAME_SIZE];
  uint8_t units[MLG_FIELD_UNITS_SIZE];
  mlg_DisplayStyle display_style;
  float scale;
  float transform;
  uint8_t digits;
  uint8_t category[MLG_FIELD_CATEGORY_SIZE];
} mlg_Field;

typedef struct {
  float num;
} test_struct;

void mlg_test() {
  mlg_Field field = {
    .type = MLG_BITS,
    .name = "name",
    .units = "units",
    .display_style = MLG_DATE,
    .scale = 1,
    .transform = 2,
    .digits = 3,
    .category = "category"
  };
  uint8_t buffer[MLG_FIELD_SIZE];
  memset(buffer, 0, MLG_FIELD_SIZE);
//  memcpy(buffer, &field, MLG_FIELD_SIZE);

  test_struct test = { 1 };
  memcpy_swapend(buffer, &test, sizeof(test));

  HAL_Delay(1);
}

//void mlg_init_field(uint8_t* buffer) {
//  buffer[0] = 0;
//}

//// Calculate size of field based on its type
//size_t mlg_getFieldSize(mlg_FieldType type) {
//  switch (type) {
//    case MLG_U08:
//    case MLG_S08:
//    case MLG_U08_BITFIELD:
//      return 1;
//    case MLG_U16:
//    case MLG_S16:
//    case MLG_U16_BITFIELD:
//      return 2;
//    case MLG_U32:
//    case MLG_S32:
//    case MLG_U32_BITFIELD:
//      return 4;
//    case MLG_S64:
//      return 8;
//    case MLG_F32:
//      return 4;
//    default:
//      return 0;
//  }
//}
//
//// Calculate total data size for scalar and bit fields
//size_t mlg_calculateDataSize(mlg_LoggerField_Scalar* scalarFields, uint8_t numScalarFields, mlg_LoggerField_Bit* bitFields, uint8_t numBitFields) {
//  size_t size = 0;
//  for (int i = 0; i < numScalarFields; ++i) {
//    size += mlg_getFieldSize(scalarFields[i].type);
//  }
//  for (int i = 0; i < numBitFields; ++i) {
//    size += mlg_getFieldSize(bitFields[i].type);
//  }
//  return size;
//}
//
//// Pack MLVLG header into buffer
//int mlg_packHeaderToBuffer(mlg_PackHeaderArgs* args) {
//  size_t offset = 0;
//
//  // Ensure buffer is large enough
//  size_t requiredSize = sizeof(mlg_Header) + args->numScalarFields * sizeof(mlg_LoggerField_Scalar) + args->numBitFields * sizeof(mlg_LoggerField_Bit);
//  if (requiredSize > args->bufferSize) {
//    return -1; // Buffer overflow
//  }
//
//  // Calculate dataBeginIndex and infoDataStart
//  args->header->dataBeginIndex = args->infoDataStartOffset + sizeof(mlg_Header) + args->numScalarFields * sizeof(mlg_LoggerField_Scalar) + args->numBitFields * sizeof(mlg_LoggerField_Bit);
//  args->header->infoDataStart = args->infoDataStartOffset;
//
//  // Copy fileFormat
//  memcpy(args->buffer + offset, args->header->fileFormat, sizeof(args->header->fileFormat));
//  offset += sizeof(args->header->fileFormat);
//
//  // Convert to big-endian and copy formatVersion
//  args->buffer[offset++] = (args->header->formatVersion >> 8) & 0xFF;
//  args->buffer[offset++] = args->header->formatVersion & 0xFF;
//
//  // Convert to big-endian and copy timeStamp
//  args->buffer[offset++] = (args->header->timeStamp >> 24) & 0xFF;
//  args->buffer[offset++] = (args->header->timeStamp >> 16) & 0xFF;
//  args->buffer[offset++] = (args->header->timeStamp >> 8) & 0xFF;
//  args->buffer[offset++] = args->header->timeStamp & 0xFF;
//
//  // Convert to big-endian and copy infoDataStart
//  args->buffer[offset++] = (args->header->infoDataStart >> 24) & 0xFF;
//  args->buffer[offset++] = (args->header->infoDataStart >> 16) & 0xFF;
//  args->buffer[offset++] = (args->header->infoDataStart >> 8) & 0xFF;
//  args->buffer[offset++] = args->header->infoDataStart & 0xFF;
//
//  // Convert to big-endian and copy dataBeginIndex
//  args->buffer[offset++] = (args->header->dataBeginIndex >> 24) & 0xFF;
//  args->buffer[offset++] = (args->header->dataBeginIndex >> 16) & 0xFF;
//  args->buffer[offset++] = (args->header->dataBeginIndex >> 8) & 0xFF;
//  args->buffer[offset++] = args->header->dataBeginIndex & 0xFF;
//
//  // Convert to big-endian and copy recordLength
//  args->buffer[offset++] = (args->header->recordLength >> 8) & 0xFF;
//  args->buffer[offset++] = args->header->recordLength & 0xFF;
//
//  // Convert to big-endian and copy numLoggerFields
//  uint16_t numLoggerFields = args->numScalarFields + args->numBitFields;
//  args->buffer[offset++] = (numLoggerFields >> 8) & 0xFF;
//  args->buffer[offset++] = numLoggerFields & 0xFF;
//
//  // Copy Scalar Logger Fields
//  for (int i = 0; i < args->numScalarFields; ++i) {
//    memcpy(args->buffer + offset, &args->scalarFields[i].type, sizeof(args->scalarFields[i].type));
//    offset += sizeof(args->scalarFields[i].type);
//
//    memcpy(args->buffer + offset, args->scalarFields[i].name, sizeof(args->scalarFields[i].name));
//    offset += sizeof(args->scalarFields[i].name);
//
//    memcpy(args->buffer + offset, args->scalarFields[i].units, sizeof(args->scalarFields[i].units));
//    offset += sizeof(args->scalarFields[i].units);
//
//    memcpy(args->buffer + offset, &args->scalarFields[i].displayStyle, sizeof(args->scalarFields[i].displayStyle));
//    offset += sizeof(args->scalarFields[i].displayStyle);
//
//    memcpy(args->buffer + offset, &args->scalarFields[i].scale, sizeof(args->scalarFields[i].scale));
//    offset += sizeof(args->scalarFields[i].scale);
//
//    memcpy(args->buffer + offset, &args->scalarFields[i].transform, sizeof(args->scalarFields[i].transform));
//    offset += sizeof(args->scalarFields[i].transform);
//
//    memcpy(args->buffer + offset, &args->scalarFields[i].digits, sizeof(args->scalarFields[i].digits));
//    offset += sizeof(args->scalarFields[i].digits);
//
//    memcpy(args->buffer + offset, args->scalarFields[i].category, sizeof(args->scalarFields[i].category));
//    offset += sizeof(args->scalarFields[i].category);
//  }
//
//  // Copy Bit Logger Fields
//  for (int i = 0; i < args->numBitFields; ++i) {
//    memcpy(args->buffer + offset, &args->bitFields[i].type, sizeof(args->bitFields[i].type));
//    offset += sizeof(args->bitFields[i].type);
//
//    memcpy(args->buffer + offset, args->bitFields[i].name, sizeof(args->bitFields[i].name));
//    offset += sizeof(args->bitFields[i].name);
//
//    memcpy(args->buffer + offset, args->bitFields[i].units, sizeof(args->bitFields[i].units));
//    offset += sizeof(args->bitFields[i].units);
//
//    memcpy(args->buffer + offset, &args->bitFields[i].displayStyle, sizeof(args->bitFields[i].displayStyle));
//    offset += sizeof(args->bitFields[i].displayStyle);
//
//    memcpy(args->buffer + offset, &args->bitFields[i].bitFieldStyle, sizeof(args->bitFields[i].bitFieldStyle));
//    offset += sizeof(args->bitFields[i].bitFieldStyle);
//
//    memcpy(args->buffer + offset, &args->bitFields[i].bitFieldNamesIndex, sizeof(args->bitFields[i].bitFieldNamesIndex));
//    offset += sizeof(args->bitFields[i].bitFieldNamesIndex);
//
//    memcpy(args->buffer + offset, &args->bitFields[i].bits, sizeof(args->bitFields[i].bits));
//    offset += sizeof(args->bitFields[i].bits);
//
//    memcpy(args->buffer + offset, args->bitFields[i].unused, sizeof(args->bitFields[i].unused));
//    offset += sizeof(args->bitFields[i].unused);
//
//    memcpy(args->buffer + offset, args->bitFields[i].category, sizeof(args->bitFields[i].category));
//    offset += sizeof(args->bitFields[i].category);
//  }
//
//  return 0; // Success
//}
//
//// Pack DataBlock into buffer
//int mlg_packDataBlock(mlg_PackDataBlockArgs* args) {
//  size_t offset = 0;
//
//  size_t dataSize = mlg_calculateDataSize(args->scalarFields, args->numScalarFields, args->bitFields, args->numBitFields);
//
//  // Ensure buffer is large enough
//  size_t requiredSize = 1 + 1 + 2 + dataSize + 1; // Type + Counter + Timestamp + Data + CRC
//  if (requiredSize > args->bufferSize) {
//    return -1; // Buffer overflow
//  }
//
//  // Copy type
//  args->buffer[offset++] = args->dataBlock->type;
//
//  // Copy counter
//  args->buffer[offset++] = args->dataBlock->counter;
//
//  // Copy timestamp
//  args->buffer[offset++] = (args->dataBlock->timestamp >> 8) & 0xFF;
//  args->buffer[offset++] = args->dataBlock->timestamp & 0xFF;
//
//  // Copy data
//  memcpy(args->buffer + offset, args->dataBlock->data, dataSize);
//  offset += dataSize;
//
//  // Calculate and copy CRC
//  uint8_t crc = 0;
//  for (size_t i = 0; i < dataSize; i++) {
//    crc += args->dataBlock->data[i];
//  }
//  args->buffer[offset++] = crc;
//
//  return 0; // Success
//}
//
//// Pack Marker into buffer
//int mlg_packMarker(uint8_t* buffer, size_t bufferSize, mlg_Marker* marker) {
//  size_t offset = 0;
//
//  // Ensure buffer is large enough
//  size_t requiredSize = 1 + 1 + 2 + 50; // Type + Counter + Timestamp + Message
//  if (requiredSize > bufferSize) {
//    return -1; // Buffer overflow
//  }
//
//  // Copy type
//  buffer[offset++] = marker->type;
//
//  // Copy counter
//  buffer[offset++] = marker->counter;
//
//  // Copy timestamp
//  buffer[offset++] = (marker->timestamp >> 8) & 0xFF;
//  buffer[offset++] = marker->timestamp & 0xFF;
//
//  // Copy message
//  memcpy(buffer + offset, marker->message, sizeof(marker->message));
//  offset += sizeof(marker->message);
//
//  return 0; // Success
//}
//
//// Print buffer as hex for verification
//void mlg_printBuffer(uint8_t* buffer, size_t size) {
//  for (size_t i = 0; i < size; i++) {
//    printf("%02X ", buffer[i]);
//  }
//  printf("\n");
//}
//
//// Test function to verify implementation
//void mlg_test() {
//  // Initialize example header
//  mlg_Header header = {
//      .fileFormat = "MLVLG",
//      .formatVersion = 0x0002,
//      .timeStamp = 0x5F4D3C2B,  // Example timestamp
//      .infoDataStart = 0x00000000,
//      .dataBeginIndex = 0x00000010, // This will be calculated dynamically
//      .recordLength = 0x0020,
//      .numLoggerFields = 8 // This will be calculated dynamically
//  };
//
//  // Initialize example scalar fields
//  mlg_LoggerField_Scalar scalarFields[4] = {
//      {MLG_U08, "U08Field", "U08", MLG_FLOAT, 1.0f, 0.0f, 2, "Category"},
//      {MLG_S08, "S08Field", "S08", MLG_HEX, 1.0f, 0.0f, 2, "Category"},
//      {MLG_U16, "U16Field", "U16", MLG_BITS, 1.0f, 0.0f, 2, "Category"},
//      {MLG_S16, "S16Field", "S16", MLG_DATE, 1.0f, 0.0f, 2, "Category"},
//  };
//
//  // Initialize example bit fields
//  mlg_LoggerField_Bit bitFields[3] = {
//      {MLG_U08_BITFIELD, "U08BitField", "U08", MLG_ON_OFF, MLG_YES_NO, 0x12345678, 8, {0, 0, 0}, "Category"},
//      {MLG_U16_BITFIELD, "U16BitField", "U16", MLG_YES_NO, MLG_HIGH_LOW, 0x23456789, 16, {0, 0, 0}, "Category"},
//      {MLG_U32_BITFIELD, "U32BitField", "U32", MLG_HIGH_LOW, MLG_ACTIVE_INACTIVE, 0x34567890, 32, {0, 0, 0}, "Category"}
//  };
//
//  // Example Bit Field Names
//  const char* bitFieldNames = "Bit0\0Bit1\0Bit2\0Bit3\0Bit4\0Bit5\0Bit6\0Bit7\0";
//
//  // Example Info Data
//  const char* infoData = "Additional Info Data";
//
//  size_t bufferSize = MAX_BUFFER_SIZE;
//  uint8_t buffer[bufferSize];
//  memset(buffer, 0, bufferSize);
//
//  // Pack header into buffer
//  mlg_PackHeaderArgs headerArgs = {
//      .header = &header,
//      .scalarFields = scalarFields,
//      .numScalarFields = 4,
//      .bitFields = bitFields,
//      .numBitFields = 3,
//      .buffer = buffer,
//      .bufferSize = bufferSize,
//      .infoDataStartOffset = sizeof(mlg_Header) + 4 * sizeof(mlg_LoggerField_Scalar) + 3 * sizeof(mlg_LoggerField_Bit) + strlen(bitFieldNames) + 1
//  };
//
//  int result = mlg_packHeaderToBuffer(&headerArgs);
//
//  if (result == -1) {
//    printf("Header buffer overflow\n");
//    return;
//  }
//
//  // Copy Bit Field Names to buffer
//  size_t bitFieldNamesLength = strlen(bitFieldNames) + 1; // Include null terminator
//  if (bitFieldNamesLength + header.dataBeginIndex > bufferSize) {
//    printf("Bit Field Names buffer overflow\n");
//    return;
//  }
//  memcpy(buffer + sizeof(mlg_Header) + 4 * sizeof(mlg_LoggerField_Scalar) + 3 * sizeof(mlg_LoggerField_Bit), bitFieldNames, bitFieldNamesLength);
//
//  // Copy Info Data to buffer
//  size_t infoDataLength = strlen(infoData) + 1; // Include null terminator
//  if (infoDataLength + header.dataBeginIndex + bitFieldNamesLength > bufferSize) {
//    printf("Info Data buffer overflow\n");
//    return;
//  }
//  memcpy(buffer + header.dataBeginIndex, infoData, infoDataLength);
//
//  // Print header buffer for verification
//  mlg_printBuffer(buffer, header.dataBeginIndex + infoDataLength);
//
//  // Example data block
//  size_t dataSize = mlg_calculateDataSize(scalarFields, 4, bitFields, 3);
//  uint8_t* data = (uint8_t*)malloc(dataSize);
//  if (data == NULL) {
//    printf("Failed to allocate memory for data\n");
//    return;
//  }
//
//  for (size_t i = 0; i < dataSize; i++) {
//    data[i] = i + 1; // Example data
//  }
//
//  mlg_DataBlock dataBlock = {0, 1, 0x1234, data, 0};
//
//  uint8_t dataBlockBuffer[256];
//  mlg_PackDataBlockArgs dataBlockArgs = {
//      .dataBlock = &dataBlock,
//      .scalarFields = scalarFields,
//      .numScalarFields = 4,
//      .bitFields = bitFields,
//      .numBitFields = 3,
//      .buffer = dataBlockBuffer,
//      .bufferSize = sizeof(dataBlockBuffer)
//  };
//
//  result = mlg_packDataBlock(&dataBlockArgs);
//
//  if (result == -1) {
//    printf("Data block buffer overflow\n");
//    free(data);
//    return;
//  }
//
//  // Print data block buffer for verification
//  mlg_printBuffer(dataBlockBuffer, 1 + 1 + 2 + dataSize + 1); // Type + Counter + Timestamp + Data + CRC
//
//  // Example marker
//  mlg_Marker marker = {1, 2, 0x5678, "Example Marker"};
//
//  uint8_t markerBuffer[256];
//  result = mlg_packMarker(markerBuffer, sizeof(markerBuffer), &marker);
//
//  if (result == -1) {
//    printf("Marker buffer overflow\n");
//    free(data);
//    return;
//  }
//
//  // Print marker buffer for verification
//  mlg_printBuffer(markerBuffer, 1 + 1 + 2 + sizeof(marker.message));
//
//  free(data);
//}
