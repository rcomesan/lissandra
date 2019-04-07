#include "binw.h"

void cx_binw_int32(char* _buffer, uint32_t* _inOutPos, int32_t _val)
{
    *((int32_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(int32_t);
}

void cx_binw_uint32(char* _buffer, uint32_t* _inOutPos, uint32_t _val)
{
    *((uint32_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(uint32_t);
}

void cx_binw_int16(char* _buffer, uint32_t* _inOutPos, int16_t _val)
{
    *((int16_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(int16_t);
}

void cx_binw_uint16(char* _buffer, uint32_t* _inOutPos, uint16_t _val)
{
    *((uint16_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(uint16_t);
}

void cx_binw_int8(char* _buffer, uint32_t* _inOutPos, int8_t _val)
{
    *((int8_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(int8_t);
}

void cx_binw_uint8(char* _buffer, uint32_t* _inOutPos, uint8_t _val)
{
    *((uint8_t*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(uint8_t);
}

void cx_binw_float(char* _buffer, uint32_t* _inOutPos, float _val)
{
    *((float*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(float);
}

void cx_binw_double(char* _buffer, uint32_t* _inOutPos, double _val)
{
    *((double*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(double);
}

void cx_binw_bool(char* _buffer, uint32_t* _inOutPos, bool _val)
{
    *((bool*)&(_buffer[*_inOutPos])) = _val;
    (*_inOutPos) += sizeof(bool);
}
