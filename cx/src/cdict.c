#include "cdict.h"
#include "mem.h"

#include <sys/syscall.h>
#include <sys/types.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_cdict_t* cx_cdict_init()
{
    cx_cdict_t* dict = CX_MEM_STRUCT_ALLOC(dict);
    dict->handle = dictionary_create();

    if (NULL != dict->handle)
    {
        dict->mutexInitialized = (0 == pthread_mutex_init(&dict->mutex, NULL));
        CX_CHECK(dict->mutexInitialized, "mutex initialization failed!");
    }
    CX_CHECK(NULL != dict->handle, "commons dictionary creation failed!");

    if (NULL != dict->handle && dict->mutexInitialized)
    {
        return dict;
    }
    
    cx_cdict_destroy(dict, NULL);
    return NULL;
}

void cx_cdict_destroy(cx_cdict_t* _cdict, cx_destroyer_cb _cb)
{
    if (NULL == _cdict) return;

    if (_cdict->mutexInitialized)
    {
        pthread_mutex_destroy(&_cdict->mutex);
        _cdict->mutexInitialized = false;
    }

    dictionary_destroy_and_destroy_elements(_cdict->handle, _cb);
    
    _cdict->handle = NULL;
    free(_cdict);
}

bool cx_cdict_get(cx_cdict_t* _cdict, const char* _key, void** _outData)
{
    CX_CHECK_NOT_NULL(_cdict);

    pthread_mutex_lock(&_cdict->mutex);
    (*_outData) = dictionary_get(_cdict->handle, _key);
    pthread_mutex_unlock(&_cdict->mutex);
    return NULL != (*_outData);
}

void cx_cdict_set(cx_cdict_t* _cdict, const char* _key, void* _data)
{
    CX_CHECK_NOT_NULL(_cdict);

    pthread_mutex_lock(&_cdict->mutex);
    dictionary_put(_cdict->handle, _key, _data);
    pthread_mutex_unlock(&_cdict->mutex);
}

void* cx_cdict_erase(cx_cdict_t* _cdict, const char* _key, cx_destroyer_cb _cb)
{
    CX_CHECK_NOT_NULL(_cdict);

    pthread_mutex_lock(&_cdict->mutex);

    void* data = NULL;
    data = dictionary_remove(_cdict->handle, _key);

    if (NULL != data && NULL != _cb)
    {
        _cb(data);
        data = NULL;
    }

    pthread_mutex_unlock(&_cdict->mutex);

    return data;
}

bool cx_cdict_contains(cx_cdict_t* _cdict, const char* _key)
{
    CX_CHECK_NOT_NULL(_cdict);
    
    bool result = false;

    pthread_mutex_lock(&_cdict->mutex);
    result = dictionary_has_key(_cdict->handle, _key);
    pthread_mutex_unlock(&_cdict->mutex);

    return result;
}

bool cx_cdict_is_empty(cx_cdict_t* _cdict)
{
    CX_CHECK_NOT_NULL(_cdict);
    return dictionary_is_empty(_cdict->handle);
}

uint32_t cx_cdict_size(cx_cdict_t* _cdict)
{
    CX_CHECK_NOT_NULL(_cdict);
    return dictionary_size(_cdict->handle);
}

void cx_cdict_clear(cx_cdict_t* _cdict, cx_destroyer_cb _cb)
{
    CX_CHECK_NOT_NULL(_cdict);

    if (NULL != _cb)
    {
        char* key = NULL;
        void* data = NULL;

        cx_cdict_iter_begin(_cdict);
        while (cx_cdict_iter_next(_cdict, &key, &data))
        {
            _cb(data);
        }
        dictionary_clean(_cdict->handle);
        cx_cdict_iter_end(_cdict);
    }
    else
    {
        pthread_mutex_lock(&_cdict->mutex);
        dictionary_clean(_cdict->handle);
        pthread_mutex_unlock(&_cdict->mutex);
    }    
}

void cx_cdict_iter_begin(cx_cdict_t* _cdict)
{
    CX_CHECK_NOT_NULL(_cdict);
    pthread_mutex_lock(&_cdict->mutex);
    
    cx_cdict_iter_first(_cdict);
}

void cx_cdict_iter_first(cx_cdict_t* _cdict)
{
    CX_CHECK_NOT_NULL(_cdict);
    CX_CHECK(syscall(__NR_gettid) == _cdict->mutex.__data.__owner, "you do not own this mutex! you must call cx_cdict_iter_begin first!");

    _cdict->iterTableIndex = 0;
    _cdict->iterElement = NULL;
}

bool cx_cdict_iter_next(cx_cdict_t* _cdict, char** _outKey, void** _outData)
{
    CX_CHECK_NOT_NULL(_cdict);
    CX_CHECK(syscall(__NR_gettid) == _cdict->mutex.__data.__owner, "you do not own this mutex! you must call cx_cdict_iter_begin first!");
    
    for (; _cdict->iterTableIndex < _cdict->handle->table_max_size; _cdict->iterTableIndex++)
    {
        if (NULL == _cdict->iterElement)
        {
            _cdict->iterElement = _cdict->handle->elements[_cdict->iterTableIndex];
        }
        
        if (NULL != _cdict->iterElement)
        {
            (*_outKey) = _cdict->iterElement->key;
            (*_outData) = _cdict->iterElement->data;
            
            _cdict->iterElement = _cdict->iterElement->next;
            if (NULL == _cdict->iterElement) _cdict->iterTableIndex++;

            return true;
        }       
    }
    return false;
}

void cx_cdict_iter_end(cx_cdict_t* _cdict)
{
    CX_CHECK_NOT_NULL(_cdict);
    pthread_mutex_unlock(&_cdict->mutex);
}
