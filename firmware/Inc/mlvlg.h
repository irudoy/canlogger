#ifndef MLG_LOGGER_H
#define MLG_LOGGER_H

#include <stdint.h>
#include <stdlib.h>

// Максимальный размер буфера 1 MB
#define MAX_BUFFER_SIZE (1024 * 1024)

// Структура заголовка MLVLG
typedef struct {
  char fileFormat[6];      // "MLVLG" padded by 0
  uint16_t formatVersion;  // Current version, 0x0002
  uint32_t timeStamp;      // Unix 32-bit timestamp
  uint32_t infoDataStart;  // Offset after LoggerField[] definitions
  uint32_t dataBeginIndex; // Address of the 1st byte containing Type-Data pairs
  uint16_t recordLength;   // Length of a single data record
  uint16_t numLoggerFields;// Number of expected Logger Fields
} __attribute__((packed)) mlg_Header;

// Типы полей
typedef enum {
  MLG_U08 = 0,
  MLG_S08 = 1,
  MLG_U16 = 2,
  MLG_S16 = 3,
  MLG_U32 = 4,
  MLG_S32 = 5,
  MLG_S64 = 6,
  MLG_F32 = 7,
  MLG_U08_BITFIELD = 10,
  MLG_U16_BITFIELD = 11,
  MLG_U32_BITFIELD = 12
} mlg_FieldType;

// Стили отображения
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

// Структура для скалярного поля логгера
typedef struct {
  mlg_FieldType type;      // Field type
  char name[34];           // ASCII, null terminated
  char units[10];          // ASCII, null terminated
  mlg_DisplayStyle displayStyle; // Display style
  float scale;             // IEEE 754 float
  float transform;         // IEEE 754 float
  uint8_t digits;          // S08, number of decimal places
  char category[34];       // Optional category, ASCII, null terminated
} __attribute__((packed)) mlg_LoggerField_Scalar;

// Структура для битового поля логгера
typedef struct {
  mlg_FieldType type;           // 10=U08_BITFIELD, 11=U16_BITFIELD, 12=U32_BITFIELD
  char name[34];                // ASCII, null terminated
  char units[10];               // ASCII, null terminated
  mlg_DisplayStyle displayStyle;// Display style for the field
  mlg_DisplayStyle bitFieldStyle; // Display style for the bit fields
  uint32_t bitFieldNamesIndex;  // Index of Bit Field Names
  uint8_t bits;                 // Number of valid bits
  uint8_t unused[3];            // Filler to maintain size
  char category[34];            // Optional category, ASCII, null terminated
} __attribute__((packed)) mlg_LoggerField_Bit;

// Структура блока данных
typedef struct {
  uint8_t type;          // Type identifier (Logger Field Data)
  uint8_t counter;       // Rolling counter
  uint16_t timestamp;    // 2-byte timestamp
  uint8_t* data;         // Pointer to data
  uint8_t crc;           // CRC byte
} __attribute__((packed)) mlg_DataBlock;

// Структура маркера
typedef struct {
  uint8_t type;          // 1 for marker
  uint8_t counter;       // Rolling counter
  uint16_t timestamp;    // 2-byte timestamp
  char message[50];      // Null-terminated message
} __attribute__((packed)) mlg_Marker;

// Функции
size_t mlg_getFieldSize(mlg_FieldType type);
size_t mlg_calculateDataSize(mlg_LoggerField_Scalar* scalarFields, uint8_t numScalarFields, mlg_LoggerField_Bit* bitFields, uint8_t numBitFields);
int mlg_packHeaderToBuffer(mlg_Header* header, mlg_LoggerField_Scalar* scalarFields, uint8_t numScalarFields, mlg_LoggerField_Bit* bitFields, uint8_t numBitFields, uint8_t* buffer, size_t bufferSize, uint32_t infoDataStartOffset);
int mlg_packDataBlock(uint8_t* buffer, size_t bufferSize, mlg_DataBlock* dataBlock, mlg_LoggerField_Scalar* scalarFields, uint8_t numScalarFields, mlg_LoggerField_Bit* bitFields, uint8_t numBitFields);
int mlg_packMarker(uint8_t* buffer, size_t bufferSize, mlg_Marker* marker);
void mlg_printBuffer(uint8_t* buffer, size_t size);
void mlg_test();

#endif // MLG_LOGGER_H
