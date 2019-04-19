#ifndef CX_CDICT_H_
#define CX_CDICT_H_

#include "cx.h"

#include <commons/collections/dictionary.h>

#include <pthread.h>

typedef struct cx_cdict_t
{
    t_dictionary*       handle;             // pointer to so-commons-lib dictionary adt.
    pthread_mutex_t     mutex;              // mutex for thread safety.
    bool                mutexInitialized;   // true if the mutex was successfully initialized.
    int32_t             iterTableIndex;     // current iterator table index. (bucket of the hashtable)
    t_hash_element*     iterElement;        // current iterator table element.
} cx_cdict_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_cdict_t*             cx_cdict_init();

void                    cx_cdict_destroy(cx_cdict_t* _cdict, cx_destroyer_cb _cb);

bool                    cx_cdict_get(cx_cdict_t* _cdict, const char* _key, void** _outData);

void                    cx_cdict_set(cx_cdict_t* _cdict, const char* _key, void* _data);

void*                   cx_cdict_erase(cx_cdict_t* _cdict, const char* _key, cx_destroyer_cb _cb);

bool                    cx_cdict_contains(cx_cdict_t* _cdict, const char* _key);

bool                    cx_cdict_is_empty(cx_cdict_t* _cdict);

uint32_t                cx_cdict_size(cx_cdict_t* _cdict);

void                    cx_cdict_clear(cx_cdict_t* _cdict, cx_destroyer_cb _cb);

void                    cx_cdict_iter_begin(cx_cdict_t* _cdict);

void                    cx_cdict_iter_first(cx_cdict_t* _cdict);

bool                    cx_cdict_iter_next(cx_cdict_t* _cdict, char** _outKey, void** _outData);

void                    cx_cdict_iter_end(cx_cdict_t* _cdict);

#endif // CX_CDICT_H_