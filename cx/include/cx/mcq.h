#ifndef CX_MCQ_H_
#define CX_MCQ_H_

#include "cx.h"

#include <pthread.h>
#include <commons/collections/queue.h>
#include <stdbool.h>

typedef struct cx_mcq_t
{
    t_queue*            handle;             // pointer to so-commons-lib queue adt.
    pthread_mutex_t     mutex;              // mutex for syncing enqueue/dequeue from multiple threads.
    pthread_cond_t      cond;               // condition to signal threads to wake up when there's more work to be done.
    bool                mutexInitialized;   // true if the mutex was successfully initialized.
    bool                condInitialized;    // true if the condition was successfully initialized.
} cx_mcq_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_mcq_t*               cx_mcq_init();

void                    cx_mcq_destroy(cx_mcq_t* _mcq, cx_destroyer_cb _cb);

void                    cx_mcq_push_first(cx_mcq_t* _mcq, void* _data);

void                    cx_mcq_push(cx_mcq_t* _mcq, void* _data);

void                    cx_mcq_pop(cx_mcq_t* _mcq, void** _outData);

int32_t                 cx_mcq_size(cx_mcq_t* _mcq);

bool            cx_mcq_is_empty(cx_mcq_t* _mcq);

#endif // CX_MCQ_H_