#include "binr.h"

//TODO this will only work on architectures of the same endianness 
// check if endian-independent code is a requirement and make the byte swaps accordingly

void cx_binr_int32(const char* _buffer, uint32_t* _inOutPos, int32_t* _outVal)
{
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(int32_t));
    (*_inOutPos) += sizeof(int32_t);
}

void cx_binr_uint32(const char* _buffer, uint32_t* _inOutPos, uint32_t* _outVal)
{
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(uint32_t));
    (*_inOutPos) += sizeof(uint32_t);
}

void cx_binr_int16(const char* _buffer, uint32_t* _inOutPos, int16_t* _outVal)
{
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(int16_t));
    (*_inOutPos) += sizeof(int16_t);
}

void cx_binr_uint16(const char* _buffer, uint32_t* _inOutPos, uint16_t* _outVal)
{
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(uint16_t));
    (*_inOutPos) += sizeof(uint16_t);
}

void cx_binr_int8(const char* _buffer, uint32_t* _inOutPos, int8_t* _outVal)
{
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(int8_t));
    (*_inOutPos) += sizeof(int8_t);
}

void cx_binr_uint8(const char* _buffer, uint32_t* _inOutPos, uint8_t* _outVal)
{
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(uint8_t));
    (*_inOutPos) += sizeof(uint8_t);
}

void cx_binr_float(const char* _buffer, uint32_t* _inOutPos, float* _outVal)
{
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(float));
    (*_inOutPos) += sizeof(float);
}

void cx_binr_double(const char* _buffer, uint32_t* _inOutPos, double* _outVal)
{
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(double));
    (*_inOutPos) += sizeof(double);
}

void cx_binr_bool(const char* _buffer, uint32_t* _inOutPos, bool* _outVal)
{
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(bool));
    (*_inOutPos) += sizeof(bool);
}
