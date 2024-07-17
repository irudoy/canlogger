#include "mlvlg.h"
#include <stdio.h>
#include <string.h>

// Calculate size of field based on its type
size_t mlg_getFieldSize(mlg_FieldType type) {
  switch (type) {
    case MLG_U08:
    case MLG_S08:
    case MLG_U08_BITFIELD:
      return 1;
    case MLG_U16:
    case MLG_S16:
    case MLG_U16_BITFIELD:
      return 2;
    case MLG_U32:
    case MLG_S32:
    case MLG_U32_BITFIELD:
      return 4;
    case MLG_S64:
      return 8;
    case MLG_F32:
      return 4;
    default:
      return 0;
  }
}

// Calculate total data size for scalar and bit fields
size_t mlg_calculateDataSize(mlg_LoggerField_Scalar* scalarFields, uint8_t numScalarFields, mlg_LoggerField_Bit* bitFields, uint8_t numBitFields) {
  size_t size = 0;
  for (int i = 0; i < numScalarFields; ++i) {
    size += mlg_getFieldSize(scalarFields[i].type);
  }
  for (int i = 0; i < numBitFields; ++i) {
    size += mlg_getFieldSize(bitFields[i].type);
  }
  return size;
}

// Pack MLVLG header into buffer
int mlg_packHeaderToBuffer(mlg_Header* header, mlg_LoggerField_Scalar* scalarFields, uint8_t numScalarFields, mlg_LoggerField_Bit* bitFields, uint8_t numBitFields, uint8_t* buffer, size_t bufferSize, uint32_t infoDataStartOffset) {
  size_t offset = 0;

  // Ensure buffer is large enough
  size_t requiredSize = sizeof(mlg_Header) + numScalarFields * sizeof(mlg_LoggerField_Scalar) + numBitFields * sizeof(mlg_LoggerField_Bit);
  if (requiredSize > bufferSize) {
    return -1; // Buffer overflow
  }

  // Calculate dataBeginIndex and infoDataStart
  header->dataBeginIndex = infoDataStartOffset + sizeof(mlg_Header) + numScalarFields * sizeof(mlg_LoggerField_Scalar) + numBitFields * sizeof(mlg_LoggerField_Bit);
  header->infoDataStart = infoDataStartOffset;

  // Copy fileFormat
  memcpy(buffer + offset, header->fileFormat, sizeof(header->fileFormat));
  offset += sizeof(header->fileFormat);

  // Convert to big-endian and copy formatVersion
  buffer[offset++] = (header->formatVersion >> 8) & 0xFF;
  buffer[offset++] = header->formatVersion & 0xFF;

  // Convert to big-endian and copy timeStamp
  buffer[offset++] = (header->timeStamp >> 24) & 0xFF;
  buffer[offset++] = (header->timeStamp >> 16) & 0xFF;
  buffer[offset++] = (header->timeStamp >> 8) & 0xFF;
  buffer[offset++] = header->timeStamp & 0xFF;

  // Convert to big-endian and copy infoDataStart
  buffer[offset++] = (header->infoDataStart >> 24) & 0xFF;
  buffer[offset++] = (header->infoDataStart >> 16) & 0xFF;
  buffer[offset++] = (header->infoDataStart >> 8) & 0xFF;
  buffer[offset++] = header->infoDataStart & 0xFF;

  // Convert to big-endian and copy dataBeginIndex
  buffer[offset++] = (header->dataBeginIndex >> 24) & 0xFF;
  buffer[offset++] = (header->dataBeginIndex >> 16) & 0xFF;
  buffer[offset++] = (header->dataBeginIndex >> 8) & 0xFF;
  buffer[offset++] = header->dataBeginIndex & 0xFF;

  // Convert to big-endian and copy recordLength
  buffer[offset++] = (header->recordLength >> 8) & 0xFF;
  buffer[offset++] = header->recordLength & 0xFF;

  // Convert to big-endian and copy numLoggerFields
  uint16_t numLoggerFields = numScalarFields + numBitFields;
  buffer[offset++] = (numLoggerFields >> 8) & 0xFF;
  buffer[offset++] = numLoggerFields & 0xFF;

  // Copy Scalar Logger Fields
  for (int i = 0; i < numScalarFields; ++i) {
    memcpy(buffer + offset, &scalarFields[i].type, sizeof(scalarFields[i].type));
    offset += sizeof(scalarFields[i].type);

    memcpy(buffer + offset, scalarFields[i].name, sizeof(scalarFields[i].name));
    offset += sizeof(scalarFields[i].name);

    memcpy(buffer + offset, scalarFields[i].units, sizeof(scalarFields[i].units));
    offset += sizeof(scalarFields[i].units);

    memcpy(buffer + offset, &scalarFields[i].displayStyle, sizeof(scalarFields[i].displayStyle));
    offset += sizeof(scalarFields[i].displayStyle);

    memcpy(buffer + offset, &scalarFields[i].scale, sizeof(scalarFields[i].scale));
    offset += sizeof(scalarFields[i].scale);

    memcpy(buffer + offset, &scalarFields[i].transform, sizeof(scalarFields[i].transform));
    offset += sizeof(scalarFields[i].transform);

    memcpy(buffer + offset, &scalarFields[i].digits, sizeof(scalarFields[i].digits));
    offset += sizeof(scalarFields[i].digits);

    memcpy(buffer + offset, scalarFields[i].category, sizeof(scalarFields[i].category));
    offset += sizeof(scalarFields[i].category);
  }

  // Copy Bit Logger Fields
  for (int i = 0; i < numBitFields; ++i) {
    memcpy(buffer + offset, &bitFields[i].type, sizeof(bitFields[i].type));
    offset += sizeof(bitFields[i].type);

    memcpy(buffer + offset, bitFields[i].name, sizeof(bitFields[i].name));
    offset += sizeof(bitFields[i].name);

    memcpy(buffer + offset, bitFields[i].units, sizeof(bitFields[i].units));
    offset += sizeof(bitFields[i].units);

    memcpy(buffer + offset, &bitFields[i].displayStyle, sizeof(bitFields[i].displayStyle));
    offset += sizeof(bitFields[i].displayStyle);

    memcpy(buffer + offset, &bitFields[i].bitFieldStyle, sizeof(bitFields[i].bitFieldStyle));
    offset += sizeof(bitFields[i].bitFieldStyle);

    memcpy(buffer + offset, &bitFields[i].bitFieldNamesIndex, sizeof(bitFields[i].bitFieldNamesIndex));
    offset += sizeof(bitFields[i].bitFieldNamesIndex);

    memcpy(buffer + offset, &bitFields[i].bits, sizeof(bitFields[i].bits));
    offset += sizeof(bitFields[i].bits);

    memcpy(buffer + offset, bitFields[i].unused, sizeof(bitFields[i].unused));
    offset += sizeof(bitFields[i].unused);

    memcpy(buffer + offset, bitFields[i].category, sizeof(bitFields[i].category));
    offset += sizeof(bitFields[i].category);
  }

  return 0; // Success
}

// Pack DataBlock into buffer
int mlg_packDataBlock(uint8_t* buffer, size_t bufferSize, mlg_DataBlock* dataBlock, mlg_LoggerField_Scalar* scalarFields, uint8_t numScalarFields, mlg_LoggerField_Bit* bitFields, uint8_t numBitFields) {
  size_t offset = 0;

  size_t dataSize = mlg_calculateDataSize(scalarFields, numScalarFields, bitFields, numBitFields);

  // Ensure buffer is large enough
  size_t requiredSize = 1 + 1 + 2 + dataSize + 1; // Type + Counter + Timestamp + Data + CRC
  if (requiredSize > bufferSize) {
    return -1; // Buffer overflow
  }

  // Copy type
  buffer[offset++] = dataBlock->type;

  // Copy counter
  buffer[offset++] = dataBlock->counter;

  // Copy timestamp
  buffer[offset++] = (dataBlock->timestamp >> 8) & 0xFF;
  buffer[offset++] = dataBlock->timestamp & 0xFF;

  // Copy data
  memcpy(buffer + offset, dataBlock->data, dataSize);
  offset += dataSize;

  // Calculate and copy CRC
  uint8_t crc = 0;
  for (size_t i = 0; i < dataSize; i++) {
    crc += dataBlock->data[i];
  }
  buffer[offset++] = crc;

  return 0; // Success
}

// Pack Marker into buffer
int mlg_packMarker(uint8_t* buffer, size_t bufferSize, mlg_Marker* marker) {
  size_t offset = 0;

  // Ensure buffer is large enough
  size_t requiredSize = 1 + 1 + 2 + 50; // Type + Counter + Timestamp + Message
  if (requiredSize > bufferSize) {
    return -1; // Buffer overflow
  }

  // Copy type
  buffer[offset++] = marker->type;

  // Copy counter
  buffer[offset++] = marker->counter;

  // Copy timestamp
  buffer[offset++] = (marker->timestamp >> 8) & 0xFF;
  buffer[offset++] = marker->timestamp & 0xFF;

  // Copy message
  memcpy(buffer + offset, marker->message, sizeof(marker->message));
  offset += sizeof(marker->message);

  return 0; // Success
}

// Print buffer as hex for verification
void mlg_printBuffer(uint8_t* buffer, size_t size) {
  for (size_t i = 0; i < size; i++) {
    printf("%02X ", buffer[i]);
  }
  printf("\n");
}

// Test function to verify implementation
void mlg_test() {
  // Initialize example header
  mlg_Header header = {
      .fileFormat = "MLVLG",
      .formatVersion = 0x0002,
      .timeStamp = 0x5F4D3C2B,  // Example timestamp
      .infoDataStart = 0x00000000,
      .dataBeginIndex = 0x00000010, // This will be calculated dynamically
      .recordLength = 0x0020,
      .numLoggerFields = 8 // This will be calculated dynamically
  };

  // Initialize example scalar fields
  mlg_LoggerField_Scalar scalarFields[4] = {
      {MLG_U08, "U08Field", "U08", MLG_FLOAT, 1.0f, 0.0f, 2, "Category"},
      {MLG_S08, "S08Field", "S08", MLG_HEX, 1.0f, 0.0f, 2, "Category"},
      {MLG_U16, "U16Field", "U16", MLG_BITS, 1.0f, 0.0f, 2, "Category"},
      {MLG_S16, "S16Field", "S16", MLG_DATE, 1.0f, 0.0f, 2, "Category"},
  };

  // Initialize example bit fields
  mlg_LoggerField_Bit bitFields[3] = {
      {MLG_U08_BITFIELD, "U08BitField", "U08", MLG_ON_OFF, MLG_YES_NO, 0x12345678, 8, {0, 0, 0}, "Category"},
      {MLG_U16_BITFIELD, "U16BitField", "U16", MLG_YES_NO, MLG_HIGH_LOW, 0x23456789, 16, {0, 0, 0}, "Category"},
      {MLG_U32_BITFIELD, "U32BitField", "U32", MLG_HIGH_LOW, MLG_ACTIVE_INACTIVE, 0x34567890, 32, {0, 0, 0}, "Category"}
  };

  // Example Bit Field Names
  const char* bitFieldNames = "Bit0\0Bit1\0Bit2\0Bit3\0Bit4\0Bit5\0Bit6\0Bit7\0";

  // Example Info Data
  const char* infoData = "Additional Info Data";

  size_t bufferSize = MAX_BUFFER_SIZE;
  uint8_t buffer[bufferSize];
  memset(buffer, 0, bufferSize);

  // Pack header into buffer
  int result = mlg_packHeaderToBuffer(&header, scalarFields, 4, bitFields, 3, buffer, bufferSize, sizeof(mlg_Header) + 4 * sizeof(mlg_LoggerField_Scalar) + 3 * sizeof(mlg_LoggerField_Bit) + strlen(bitFieldNames) + 1);

  if (result == -1) {
    printf("Header buffer overflow\n");
    return;
  }

  // Copy Bit Field Names to buffer
  size_t bitFieldNamesLength = strlen(bitFieldNames) + 1; // Include null terminator
  if (bitFieldNamesLength + header.dataBeginIndex > bufferSize) {
    printf("Bit Field Names buffer overflow\n");
    return;
  }
  memcpy(buffer + sizeof(mlg_Header) + 4 * sizeof(mlg_LoggerField_Scalar) + 3 * sizeof(mlg_LoggerField_Bit), bitFieldNames, bitFieldNamesLength);

  // Copy Info Data to buffer
  size_t infoDataLength = strlen(infoData) + 1; // Include null terminator
  if (infoDataLength + header.dataBeginIndex + bitFieldNamesLength > bufferSize) {
    printf("Info Data buffer overflow\n");
    return;
  }
  memcpy(buffer + header.dataBeginIndex, infoData, infoDataLength);

  // Print header buffer for verification
  mlg_printBuffer(buffer, header.dataBeginIndex + infoDataLength);

  // Example data block
  size_t dataSize = mlg_calculateDataSize(scalarFields, 4, bitFields, 3);
  uint8_t* data = (uint8_t*)malloc(dataSize);
  if (data == NULL) {
    printf("Failed to allocate memory for data\n");
    return;
  }

  for (size_t i = 0; i < dataSize; i++) {
    data[i] = i + 1; // Example data
  }

  mlg_DataBlock dataBlock = {0, 1, 0x1234, data, 0};

  uint8_t dataBlockBuffer[256];
  result = mlg_packDataBlock(dataBlockBuffer, sizeof(dataBlockBuffer), &dataBlock, scalarFields, 4, bitFields, 3);

  if (result == -1) {
    printf("Data block buffer overflow\n");
    free(data);
    return;
  }

  // Print data block buffer for verification
  mlg_printBuffer(dataBlockBuffer, 1 + 1 + 2 + dataSize + 1); // Type + Counter + Timestamp + Data + CRC

  // Example marker
  mlg_Marker marker = {1, 2, 0x5678, "Example Marker"};

  uint8_t markerBuffer[256];
  result = mlg_packMarker(markerBuffer, sizeof(markerBuffer), &marker);

  if (result == -1) {
    printf("Marker buffer overflow\n");
    free(data);
    return;
  }

  // Print marker buffer for verification
  mlg_printBuffer(markerBuffer, 1 + 1 + 2 + sizeof(marker.message));

  free(data);
}
