#include "str.h"
#include "cx.h"
#include "math.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>

bool cx_str_is_empty(const char* _a)
{
    return false
        || NULL == _a 
        || strlen(_a) <= 0;
}

char* cx_str_cat_d(const char* _a, const char* _b)
{
    CX_CHECK_NOT_NULL(_a);
    CX_CHECK_NOT_NULL(_b);

    uint32_t aLen = strlen(_a);
    uint32_t bLen = strlen(_b);
    uint32_t totalLen = aLen + bLen;

    char* newString;
    newString = malloc(totalLen + 1);

    memcpy(newString, _a, aLen);
    memcpy(newString + aLen, _b, bLen);
    newString[totalLen] = '\0';

    return newString;
}

uint32_t cx_str_cat(char* _dst, uint32_t _dstSize, const char* _src)
{
    CX_CHECK_NOT_NULL(_dst);
    CX_CHECK_NOT_NULL(_src);
    CX_CHECK(_dstSize > 0, "_dstSize must be greater than zero");

    uint32_t dstLen = strlen(_dst);
    uint32_t srcLen = strlen(_src);
    uint32_t numBytes = cx_math_min(_dstSize - dstLen - 1, srcLen);
    memcpy(_dst + dstLen, _src, numBytes);
    _dst[dstLen + numBytes] = '\0';

    CX_WARN(srcLen <= numBytes, "_src string truncated (%d characters lost)", srcLen - numBytes);

    return dstLen + numBytes;
}

uint32_t cx_str_copy(char* _dst, uint32_t _dstSize, const char* _src)
{
    CX_CHECK_NOT_NULL(_dst);
    CX_CHECK_NOT_NULL(_src);
    CX_CHECK(_dstSize > 0, "_dstSize must be greater than zero");

    _dst[0] = '\0';
    return cx_str_cat(_dst, _dstSize, _src);
}

char* cx_str_copy_d(const char* _src)
{
    CX_CHECK_NOT_NULL(_src);
    
    return strdup(_src);
}

void cx_str_to_upper(char* _a)
{
    CX_CHECK_NOT_NULL(_a);   
    char* s = _a;
    while (*s)
    {
        (*s) = (char)toupper((int32_t)*s);
        s++;
    }
}

void cx_str_to_lower(char* _a)
{
    CX_CHECK_NOT_NULL(_a);
    char* s = _a;
    while (*s)
    {
        (*s) = (char)tolower((int32_t)*s);
        s++;
    }
}

bool cx_str_ends_with(const char* _a, const char* _end, bool _caseInsensitive)
{
    uint32_t aLen = strlen(_a);
    uint32_t endLen = strlen(_end);

    CX_CHECK(endLen > 0, "_end is an empty string!");
    if (endLen > aLen || endLen == 0) return false;

    if (_caseInsensitive)
    {
        return (0 == strncasecmp(&_a[aLen - endLen], _end, endLen));
    }
    else
    {
        return (0 == strncmp(&_a[aLen - endLen], _end, endLen));
    }
}

bool cx_str_starts_with(const char* _a, const char* _start, bool _caseInsensitive)
{
    uint32_t startLen = strlen(_start);
    
    CX_CHECK(startLen > 0, "_start is an empty string!");
    if (startLen == 0) return false;

    if (_caseInsensitive)
    {
        return (0 == strncasecmp(_a, _start, startLen));
    }
    else
    {
        return (0 == strncmp(_a, _start, startLen));
    }
}

bool cx_str_to_int32(const char* _src, int32_t* _out)
{
    char* end;

    errno = 0;
    int32_t i32 = strtol(_src, &end, 10);

    if (errno || end == _src || *end != '\0')
    {
        return false;
    }

    (*_out) = i32;
    return true;
}

bool cx_str_to_uint32(const char* _src, uint32_t* _out)
{
    char* end;

    errno = 0;
    uint32_t ui32 = strtoul(_src, &end, 10);

    if (errno || end == _src || *end != '\0')
    {
        return false;
    }

    (*_out) = ui32;
    return true;
}

bool cx_str_to_int16(const char* _src, int16_t* _out)
{
    int32_t i32 = 0;

    if (true
        && cx_str_to_int32(_src, &i32)
        && cx_math_in_range(i32, INT16_MIN, INT16_MAX))
    {
        (*_out) = (int16_t)i32;
        return true;
    }
    return false;
}

bool cx_str_to_uint16(const char* _src, uint16_t* _out)
{
    uint32_t ui32 = 0;

    if (true
        && cx_str_to_uint32(_src, &ui32)
        && cx_math_in_range(ui32, 0, UINT16_MAX))
    {
        (*_out) = (uint16_t)ui32;
        return true;
    }
    return false;
}

bool cx_str_to_int8(const char* _src, int8_t* _out)
{
    int32_t i32 = 0;

    if (true
        && cx_str_to_int32(_src, &i32)
        && cx_math_in_range(i32, INT8_MIN, INT8_MAX))
    {
        (*_out) = (int8_t)i32;
        return true;
    }
    return false;
}

bool cx_str_to_uint8(const char* _src, uint8_t* _out)
{
    uint32_t ui32 = 0;

    if (true
        && cx_str_to_uint32(_src, &ui32)
        && cx_math_in_range(ui32, 0, UINT8_MAX))
    {
        (*_out) = (uint8_t)ui32;
        return true;
    }
    return false;
}

int32_t cx_str_from_int32(int32_t _value, char* _buffer, uint32_t _bufferSize)
{
    return snprintf(_buffer, _bufferSize, "%" PRId32, _value);
}

int32_t cx_str_from_uint32(uint32_t _value, char* _buffer, uint32_t _bufferSize)
{
    return snprintf(_buffer, _bufferSize, "%" PRIu32, _value);
}

int32_t cx_str_from_int16(int16_t _value, char* _buffer, uint32_t _bufferSize)
{
    return snprintf(_buffer, _bufferSize, "%" PRId16, _value);
}

int32_t cx_str_from_uint16(uint16_t _value, char* _buffer, uint32_t _bufferSize)
{
    return snprintf(_buffer, _bufferSize, "%" PRIu16, _value);
}

int32_t cx_str_from_int8(int8_t _value, char* _buffer, uint32_t _bufferSize)
{
    return snprintf(_buffer, _bufferSize, "%" PRId8, _value);
}

int32_t cx_str_from_uint8(uint8_t _value, char* _buffer, uint32_t _bufferSize)
{
    return snprintf(_buffer, _bufferSize, "%" PRIu8, _value);
}

uint32_t cx_str_format(char* _buffer, uint32_t _bufferSize, const char* _format, ...)
{
    va_list args;
    va_start(args, _format);
    uint32_t len = vsnprintf(_buffer, _bufferSize, _format, args);
    va_end(args);
    return len;
}

char* cx_str_format_d(const char* _format, ...)
{
    va_list args;
    va_start(args, _format);

    uint32_t len = vsnprintf(NULL, 0, _format, args) + 1;
    char* buffer = malloc(len);
    vsnprintf(buffer, len, _format, args);

    va_end(args);

    return buffer;
}
