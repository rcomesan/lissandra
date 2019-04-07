#include "cx.h"
#include "mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t cx_arr_size(void** _arr)
{
    CX_CHECK_NOT_NULL(_arr);

    uint32_t size = 0;
    while (NULL != _arr[size])
    {
        size++;
    }

    return size;
}

void cx_arr_nfree(void** _arr, uint32_t _numElements, cx_arr_free_cb_t _freeCb)
{
    CX_CHECK_NOT_NULL(_arr);

    for (uint32_t i = 0; i < _numElements; i++)
    {
        _freeCb(_arr[i]);
    }
    free(_arr);
}

void cx_arr_free(void** _arr, cx_arr_free_cb_t _freeCb)
{
    CX_CHECK_NOT_NULL(_arr);

    uint32_t i = 0;
    while (NULL != _arr[i])
    {
        _freeCb(_arr[i]);
        i++;
    }
    free(_arr);
}

