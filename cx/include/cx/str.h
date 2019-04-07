#ifndef CX_STR_H_
#define CX_STR_H_

#include <stdint.h>
#include <stdbool.h>

bool        cx_str_is_empty(const char* _a);

char*       cx_str_cat_d(const char* _a, const char* _b);

uint32_t    cx_str_cat(char* _dst, uint32_t _dstSize, const char* _src);

uint32_t    cx_str_copy(char* _dst, uint32_t _dstSize, const char* _src);

char*       cx_str_copy_d(const char* _src);

int32_t     cx_str_parse_int(const char* _src);

uint32_t    cx_str_format(char* _buffer, uint32_t _bufferSize, const char* _format, ...);

char*       cx_str_format_d(const char* _format, ...);

#endif // CX_STR_H_
