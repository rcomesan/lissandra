#ifndef CX_MEM_H_
#define CX_MEM_H_

#include "cx.h"
#include <stdint.h>
#include <string.h>

#define CX_MEM_STRUCT_ALLOC(_var)                               \
    malloc(sizeof(*(_var)));                                    \
    memset(_var, 0, sizeof(*(_var)));                           \
    CX_CHECK(_var, #_var " struct allocation failed!");

#define CX_MEM_ARR_ALLOC(_arrPtr, _numElems)                    \
    malloc((_numElems) * sizeof(*(_arrPtr)));                   \
    memset((_arrPtr), 0, (_numElems) * sizeof(*(_arrPtr)));     \
    CX_CHECK((_arrPtr), #_arrPtr " array of struct allocation failed! (%d bytes needed for %d elements)", (_numElems));

#define CX_MEM_ARR_REALLOC(_arrPtr, _numElems)                  \
    realloc((_arrPtr), (_numElems) * sizeof(*(_arrPtr)));       \
    CX_CHECK((_arrPtr), #_arrPtr " array of struct reallocation failed! (%d bytes needed for %d elements)", (_numElems) * sizeof(*(_arrPtr)), (_numElems));

#define CX_MEM_ZERO(_var)                                       \
    memset(&(_var), 0, sizeof((_var)));

#define CX_MEM_ENSURE_CAPACITY(_arrPtr, _numElems, _capacity)   \
    if (((_numElems) + 1) > (_capacity))                        \
    {                                                           \
        _capacity *= 2;                                         \
        (_arrPtr) = CX_MEM_ARR_REALLOC((_arrPtr), (_capacity)); \
    }

#define CX_ARR_SIZE(_arr) (sizeof((_arr)) / sizeof(*(_arr)))

typedef void(*cx_arr_free_cb_t)(void* _mem);

uint32_t    cx_arr_size(void** _arr);

void        cx_arr_free(void** _arr, cx_arr_free_cb_t _freeCb);

void        cx_arr_nfree(void** _arr, uint32_t _numElements, cx_arr_free_cb_t _freeCb);

#endif // CX_MEM_H_
