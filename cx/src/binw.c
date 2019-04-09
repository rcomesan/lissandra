#include "cx.h"
#include "binw.h"

void cx_binw_int32(char* _buffer, uint16_t _bufferSize, uint32_t* _inOutPos, int32_t _val)
{
    CX_CHECK((*_inOutPos) + sizeof(int32_t) <= _buffer, "out of buffer space!!");
    *((int32_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(int32_t);
}

void cx_binw_uint32(char* _buffer, uint16_t _size, uint32_t* _inOutPos, uint32_t _val)
{
    CX_CHECK((*_inOutPos) + sizeof(uint32_t) <= _buffer, "out of buffer space!!");
    *((uint32_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(uint32_t);
}

void cx_binw_int16(char* _buffer, uint16_t _size, uint32_t* _inOutPos, int16_t _val)
{
    CX_CHECK((*_inOutPos) + sizeof(int16_t) <= _buffer, "out of buffer space!!");
    *((int16_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(int16_t);
}

void cx_binw_uint16(char* _buffer, uint16_t _size, uint32_t* _inOutPos, uint16_t _val)
{
    CX_CHECK((*_inOutPos) + sizeof(uint16_t) <= _buffer, "out of buffer space!!");
    *((uint16_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(uint16_t);
}

void cx_binw_int8(char* _buffer, uint16_t _size, uint32_t* _inOutPos, int8_t _val)
{
    CX_CHECK((*_inOutPos) + sizeof(int8_t) <= _buffer, "out of buffer space!!");
    *((int8_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(int8_t);
}

void cx_binw_uint8(char* _buffer, uint16_t _size, uint32_t* _inOutPos, uint8_t _val)
{
    CX_CHECK((*_inOutPos) + sizeof(uint8_t) <= _buffer, "out of buffer space!!");
    *((uint8_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(uint8_t);
}

void cx_binw_float(char* _buffer, uint16_t _size, uint32_t* _inOutPos, float _val)
{
    CX_CHECK((*_inOutPos) + sizeof(float) <= _buffer, "out of buffer space!!");
    *((float*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(float);
}

void cx_binw_double(char* _buffer, uint16_t _size, uint32_t* _inOutPos, double _val)
{
    CX_CHECK((*_inOutPos) + sizeof(double) <= _buffer, "out of buffer space!!");
    *((double*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(double);
}

void cx_binw_bool(char* _buffer, uint16_t _size, uint32_t* _inOutPos, bool _val)
{
    CX_CHECK((*_inOutPos) + sizeof(bool) <= _buffer, "out of buffer space!!");
    *((bool*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(bool);
}
