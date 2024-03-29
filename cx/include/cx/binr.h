#ifndef CX_BINR_H_
#define CX_BINR_H_

#include <stdint.h>
#include <stdbool.h>

void        cx_binr_int64(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, int64_t* _outVal);

void        cx_binr_uint64(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, uint64_t* _outVal);

void        cx_binr_int32(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, int32_t* _outVal);

void        cx_binr_uint32(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, uint32_t* _outVal);

void        cx_binr_int16(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, int16_t* _outVal);

void        cx_binr_uint16(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, uint16_t* _outVal);

void        cx_binr_int8(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, int8_t* _outVal);

void        cx_binr_uint8(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, uint8_t* _outVal);

void        cx_binr_float(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, float* _outVal);

void        cx_binr_double(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, double* _outVal);

void        cx_binr_bool(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, bool* _outVal);

uint16_t    cx_binr_str(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, char* _outStr, uint32_t _strSize);

#endif // CX_BINR_H_