#include "mcq.h"
#include "mem.h"

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_mcq_t* cx_mcq_init()
{
    cx_mcq_t* mcq = CX_MEM_STRUCT_ALLOC(mcq);
    mcq->handle = queue_create();

    if (NULL != mcq->handle)
    {
        mcq->mutexInitialized = (0 == pthread_mutex_init(&mcq->mutex, NULL));
        CX_CHECK(mcq->mutexInitialized, "mutex initialization failed!");

        mcq->condInitialized = (0 == pthread_cond_init(&mcq->cond, NULL));
        CX_CHECK(mcq->mutexInitialized, "cond initialization failed!");
    }
    CX_CHECK(NULL != mcq->handle, "commons queue creation failed!");
    
    if (NULL != mcq->handle && mcq->mutexInitialized && mcq->condInitialized)
    {
        return mcq;
    }
    
    cx_mcq_destroy(&mcq, NULL);
    return NULL;
}

void cx_mcq_destroy(cx_mcq_t* _mcq, cx_destroyer_cb _cb)
{
    if (NULL == _mcq) return;

    if (_mcq->mutexInitialized)
    {
        pthread_mutex_destroy(&_mcq->mutex);
        _mcq->mutexInitialized = false;
    }
        
    if (_mcq->condInitialized)
    {
        pthread_cond_destroy(&_mcq->cond);
        _mcq->condInitialized = false;
    }

    if (NULL != _cb)
    {
        queue_destroy_and_destroy_elements(_mcq->handle, _cb);
    }
    else
    {
        queue_destroy(_mcq->handle);
    }
    _mcq->handle = NULL;
    free(_mcq);
}

void cx_mcq_push_first(cx_mcq_t* _mcq, void* _data)
{
    pthread_mutex_lock(&_mcq->mutex);
    
        // push the new task to the head of the linked list used by the queue implementation
        list_add_in_index(_mcq->handle->elements, 0, _data);

        // let the workers know there're more tasks waiting to be processed
        pthread_cond_signal(&_mcq->cond);

    pthread_mutex_unlock(&_mcq->mutex);
}

void cx_mcq_push(cx_mcq_t* _mcq, void* _data)
{
    pthread_mutex_lock(&_mcq->mutex);
    
        // push the new task to the queue
        queue_push(_mcq->handle, _data);

        // let the workers know there're more tasks waiting to be processed
        pthread_cond_signal(&_mcq->cond);

    pthread_mutex_unlock(&_mcq->mutex);    
}

void cx_mcq_pop(cx_mcq_t* _mcq, void** _outData)
{
    pthread_mutex_lock(&_mcq->mutex);

        while (cx_mcq_is_empty(_mcq)) // avoids spurious wakeups
        {
            pthread_cond_wait(&_mcq->cond, &_mcq->mutex);
        }

        (*_outData) = queue_pop(_mcq->handle);

    pthread_mutex_unlock(&_mcq->mutex);
}

int32_t cx_mcq_size(cx_mcq_t* _mcq)
{
    return queue_size(_mcq->handle);
}

bool cx_mcq_is_empty(cx_mcq_t* _mcq)
{
    return queue_is_empty(_mcq->handle);
}
