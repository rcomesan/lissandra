#ifndef CX_BINW_H_
#define CX_BINW_H_

#include <stdint.h>
#include <stdbool.h>

void    cx_binw_int32(char* _buffer, uint32_t* _inOutPos, int32_t _val);

void    cx_binw_uint32(char* _buffer, uint32_t* _inOutPos, uint32_t _val);

void    cx_binw_int16(char* _buffer, uint32_t* _inOutPos, int16_t _val);

void    cx_binw_uint16(char* _buffer, uint32_t* _inOutPos, uint16_t _val);

void    cx_binw_int8(char* _buffer, uint32_t* _inOutPos, int8_t _val);

void    cx_binw_uint8(char* _buffer, uint32_t* _inOutPos, uint8_t _val);

void    cx_binw_float(char* _buffer, uint32_t* _inOutPos, float _val);

void    cx_binw_double(char* _buffer, uint32_t* _inOutPos, double _val);

void    cx_binw_bool(char* _buffer, uint32_t* _inOutPos, bool _val);

#endif // CX_BINW_H_