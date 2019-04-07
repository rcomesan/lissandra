#ifndef CX_HALLOC_H_
#define CX_HALLOC_H_

#include <commons/collections/dictionary.h>
#include <stdint.h>
#include <stdbool.h>

#define INVALID_HANDLE UINT16_MAX

typedef struct cx_handle_alloc_t
{
    uint16_t            numHandles;         // current amount of handles stored
    uint16_t            maxHandles;         // maximum amount of handles that fit in our buffer
    uint16_t*           handleToIndex;      // array containing maps to convert from handle to index
    uint16_t*           indexToHandle;      // array containing maps to convert from index to handle
    uint32_t*           handleToKey;        // array containing maps to convert from handle to int32_t keys
    t_dictionary*       keyToHandle;        // dictionary containing maps to convert from key to handle
    char                tempKey[5];         // temporary pre-allocated buffer for converting 4 byte keys to a valid C string
} cx_handle_alloc_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_handle_alloc_t*      cx_halloc_init(uint16_t _maxHandles);

void                    cx_halloc_destroy(cx_handle_alloc_t* _halloc);

void                    cx_halloc_reset(cx_handle_alloc_t* _halloc);

uint16_t                cx_handle_alloc(cx_handle_alloc_t* _halloc);

uint16_t                cx_handle_alloc_key(cx_handle_alloc_t* _halloc, int32_t _key);

void                    cx_handle_free(cx_handle_alloc_t* _halloc, uint16_t _handle);

uint16_t                cx_handle_capacity(cx_handle_alloc_t* _halloc);

uint16_t                cx_handle_count(cx_handle_alloc_t* _halloc);

bool                    cx_handle_is_valid(cx_handle_alloc_t* _halloc, uint16_t _handle);

bool                    cx_handle_contains(cx_handle_alloc_t* _halloc, int32_t _key);

uint16_t                cx_handle_at(cx_handle_alloc_t* _halloc, uint16_t _index);

uint16_t                cx_handle_get(cx_handle_alloc_t* _halloc, int32_t _key);

#endif // CX_HALLOC_H_
