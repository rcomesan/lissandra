#include "cx.h"
#include "halloc.h"
#include "mem.h"

#include <string.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_handle_alloc_t* cx_halloc_init(uint16_t _maxHandles)
{
    CX_CHECK(_maxHandles < UINT16_MAX, "_maxHandles must be less than %d", UINT16_MAX);

    cx_handle_alloc_t* halloc = CX_MEM_STRUCT_ALLOC(halloc);
    halloc->maxHandles = _maxHandles;
    halloc->indexToHandle = CX_MEM_ARR_ALLOC(halloc->indexToHandle, _maxHandles);
    halloc->handleToIndex = CX_MEM_ARR_ALLOC(halloc->handleToIndex, _maxHandles);
    halloc->handleToKey = CX_MEM_ARR_ALLOC(halloc->handleToKey, _maxHandles);
    halloc->keyToHandle = dictionary_create();

    cx_halloc_reset(halloc);
    return halloc;
}

void cx_halloc_destroy(cx_handle_alloc_t* _halloc)
{
    if (NULL != _halloc)
    {
        free(_halloc->indexToHandle);
        free(_halloc->handleToIndex);
        free(_halloc->handleToKey);
        if (NULL != _halloc->keyToHandle)
        {
            dictionary_destroy(_halloc->keyToHandle);
            _halloc->keyToHandle = NULL;
        }
        free(_halloc);
    }
}

void cx_halloc_reset(cx_handle_alloc_t* _halloc)
{
    CX_CHECK_NOT_NULL(_halloc);

    _halloc->numHandles = 0;
    dictionary_clean(_halloc->keyToHandle);
    memset(_halloc->handleToIndex, 0, _halloc->maxHandles * sizeof(*(_halloc->handleToIndex)));
    memset(_halloc->handleToKey, 255, _halloc->maxHandles * sizeof(*(_halloc->handleToKey)));

    for (uint16_t i = 0; i < _halloc->maxHandles; i++)
    {
        _halloc->indexToHandle[i] = i;
    }
}

uint16_t cx_handle_alloc(cx_handle_alloc_t* _halloc)
{
    CX_CHECK_NOT_NULL(_halloc);

    if (_halloc->numHandles < _halloc->maxHandles)
    {
        uint16_t index = _halloc->numHandles;
        _halloc->numHandles++;

        uint16_t handle = _halloc->indexToHandle[index];
        _halloc->handleToIndex[handle] = index;

        return handle;
    }

    return INVALID_HANDLE;
}

uint16_t cx_handle_alloc_key(cx_handle_alloc_t* _halloc, int32_t _key)
{
    CX_CHECK_NOT_NULL(_halloc);
    CX_CHECK(_key > INT32_MIN, "_key must have a value greater than INT32_MIN (%d)", INT32_MIN);

    if (!cx_handle_contains(_halloc, _key))
    {
        uint16_t handle = cx_handle_alloc(_halloc);
        if (INVALID_HANDLE != handle)
        {
            _halloc->handleToKey[handle] = _key;
            dictionary_put(_halloc->keyToHandle, _halloc->tempKey, (void*)((int32_t)handle));
            return handle;
        }
    }

    return INVALID_HANDLE;
}

void cx_handle_free(cx_handle_alloc_t* _halloc, uint16_t _handle)
{
    CX_CHECK_NOT_NULL(_halloc);
    _halloc->numHandles--;

    // swap the current freed handle and index with the last element to fill the gap
    uint16_t index = _halloc->handleToIndex[_handle];
    uint16_t lastHandle = _halloc->indexToHandle[_halloc->numHandles];
    _halloc->indexToHandle[index] = lastHandle;
    _halloc->handleToIndex[lastHandle] = index;

    // set the last element pointing to the freed handle
    _halloc->indexToHandle[_halloc->numHandles] = _handle;

    int32_t key = _halloc->handleToKey[_handle];
    if (key > INT32_MIN) // valid key in use
    {
        // remove it now
        memcpy(_halloc->tempKey, &key, sizeof(key));
        _halloc->tempKey[4] = '\0';

        dictionary_remove(_halloc->keyToHandle, _halloc->tempKey);
        _halloc->handleToKey[_handle] = INT32_MIN;
    }
}

uint16_t cx_handle_capacity(cx_handle_alloc_t* _halloc)
{
    return _halloc->maxHandles;
}

uint16_t cx_handle_count(cx_handle_alloc_t* _halloc)
{
    return _halloc->numHandles;
}

bool cx_handle_is_valid(cx_handle_alloc_t* _halloc, uint16_t _handle)
{
    return true
        && INVALID_HANDLE != _handle
        && _halloc->handleToIndex[_handle] < _halloc->numHandles
        && _halloc->indexToHandle[_halloc->handleToIndex[_handle]] == _handle;
}

bool cx_handle_contains(cx_handle_alloc_t* _halloc, int32_t _key)
{
    memcpy(_halloc->tempKey, &_key, sizeof(_key));
    _halloc->tempKey[4] = '\0';

    return dictionary_has_key(_halloc->keyToHandle, _halloc->tempKey);
}

uint16_t cx_handle_at(cx_handle_alloc_t* _halloc, uint16_t _index)
{
    return _halloc->indexToHandle[_index];
}

uint16_t cx_handle_get(cx_handle_alloc_t* _halloc, int32_t _key)
{
    if (cx_handle_contains(_halloc, _key))
    {
        return (int32_t)dictionary_get(_halloc->keyToHandle, _halloc->tempKey);
    }
    return INVALID_HANDLE;
}
