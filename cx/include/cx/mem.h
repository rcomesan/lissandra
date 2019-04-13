#ifndef CX_MEM_H_
#define CX_MEM_H_

#include "cx.h"
#include <stdint.h>

#define CX_MEM_STRUCT_ALLOC(_var)                           \
    malloc(sizeof(*(_var)));                                \
    memset(_var, 0, sizeof(*(_var)));                       \
    CX_CHECK(_var, #_var " struct allocation failed!");

#define CX_MEM_ARR_ALLOC(_var, _numElems)                   \
    malloc((_numElems) * sizeof(*(_var)));                  \
    memset((_var), 0, (_numElems) * sizeof(*(_var)));       \
    CX_CHECK((_var), #_var " array of struct allocation failed! (%d bytes needed for %d elements)", (_numElems));

#define CX_MEM_ARR_REALLOC(_var, _numElements)              \
    realloc((_var), (_numElements) * sizeof(*(_var)));      \
    CX_CHECK((_var), #_var " array of struct reallocation failed! (%d bytes needed for %d elements)", (_numElems) * sizeof(*(_var)), (_numElems));

#define CX_MEM_ZERO(_var)                                   \
    memset(&(_var), 0, sizeof((_var)));

typedef void(*cx_arr_free_cb_t)(void* _mem);

uint32_t    cx_arr_size(void** _arr);

void        cx_arr_free(void** _arr, cx_arr_free_cb_t _freeCb);

void        cx_arr_nfree(void** _arr, uint32_t _numElements, cx_arr_free_cb_t _freeCb);

#endif // CX_MEM_H_
