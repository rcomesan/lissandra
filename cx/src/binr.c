#include "cx.h"
#include "binr.h"
#include "math.h"

#include <string.h>

void cx_binr_int64(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, int64_t* _outVal)
{
    CX_CHECK((*_inOutPos) + sizeof(int64_t) <= _bufferSize, "out of buffer space!!");
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(int64_t));
    (*_inOutPos) += sizeof(int64_t);
}

void cx_binr_uint64(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, uint64_t* _outVal)
{
    CX_CHECK((*_inOutPos) + sizeof(uint64_t) <= _bufferSize, "out of buffer space!!");
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(uint64_t));
    (*_inOutPos) += sizeof(uint64_t);
}

void cx_binr_int32(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, int32_t* _outVal)
{
    CX_CHECK((*_inOutPos) + sizeof(int32_t) <= _bufferSize, "out of buffer space!!");
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(int32_t));
    (*_inOutPos) += sizeof(int32_t);
}

void cx_binr_uint32(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, uint32_t* _outVal)
{
    CX_CHECK((*_inOutPos) + sizeof(uint32_t) <= _bufferSize, "out of buffer space!!");
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(uint32_t));
    (*_inOutPos) += sizeof(uint32_t);
}

void cx_binr_int16(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, int16_t* _outVal)
{
    CX_CHECK((*_inOutPos) + sizeof(int16_t) <= _bufferSize, "out of buffer space!!");
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(int16_t));
    (*_inOutPos) += sizeof(int16_t);
}

void cx_binr_uint16(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, uint16_t* _outVal)
{
    CX_CHECK((*_inOutPos) + sizeof(uint16_t) <= _bufferSize, "out of buffer space!!");
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(uint16_t));
    (*_inOutPos) += sizeof(uint16_t);
}

void cx_binr_int8(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, int8_t* _outVal)
{
    CX_CHECK((*_inOutPos) + sizeof(int8_t) <= _bufferSize, "out of buffer space!!");
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(int8_t));
    (*_inOutPos) += sizeof(int8_t);
}

void cx_binr_uint8(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, uint8_t* _outVal)
{
    CX_CHECK((*_inOutPos) + sizeof(uint8_t) <= _bufferSize, "out of buffer space!!");
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(uint8_t));
    (*_inOutPos) += sizeof(uint8_t);
}

void cx_binr_float(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, float* _outVal)
{
    CX_CHECK((*_inOutPos) + sizeof(float) <= _bufferSize, "out of buffer space!!");
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(float));
    (*_inOutPos) += sizeof(float);
}

void cx_binr_double(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, double* _outVal)
{
    CX_CHECK((*_inOutPos) + sizeof(double) <= _bufferSize, "out of buffer space!!");
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(double));
    (*_inOutPos) += sizeof(double);
}

void cx_binr_bool(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, bool* _outVal)
{
    CX_CHECK((*_inOutPos) + sizeof(bool) <= _bufferSize, "out of buffer space!!");
    memcpy(_outVal, &_buffer[*_inOutPos], sizeof(bool));
    (*_inOutPos) += sizeof(bool);
}

uint16_t cx_binr_str(const char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, char* _outStr, uint32_t _strSize)
{
    uint16_t strLen = 0;
    cx_binr_uint16(_buffer, _bufferSize, _inOutPos, &strLen);

    if (NULL == _outStr)
    {
        (*_inOutPos) -= sizeof(uint16_t);
        return strLen;
    }
    else
    {
        uint16_t bytesCopied = cx_math_min(strLen, _strSize - 1);
        memcpy(_outStr, &(_buffer[*_inOutPos]), bytesCopied);
        _outStr[bytesCopied] = '\0';
        (*_inOutPos) += bytesCopied;
        return bytesCopied;
    }
}
