#ifndef CX_POOL_H_
#define CX_POOL_H_

#include "mcq.h"

#include <stdint.h>
#include <pthread.h>

typedef struct cx_pool_worker_t cx_pool_worker_t;
typedef struct cx_pool_t cx_pool_t;
typedef struct cx_pool_handler_args_t cx_pool_handler_args_t;

typedef void(*cx_pool_handler_cb)(void* _data);

typedef struct cx_pool_worker_t
{
    uint16_t            index;              // worker identifier.
    pthread_t           thread;             // pthread instances for worker.
    bool                isRunning;          // true if this worker is currently running. false if it returned from the main loop.
    bool                isWaiting;          // true if this worker is currently idle waiting for more work.
};

typedef struct cx_pool_t
{
    char                name[32];           // descriptive context name for debugging purposes.
    bool                isRunning;          // true if the pool of workers is running. (successfully initialized)
    cx_pool_worker_t*   workers;            // container for workers of our thread pool.
    uint16_t            workersCount;       // number of workers in our thread pool.    
    cx_mcq_t*           queue;              // multi-consumer queue which holds the tasks available to the pool.
    cx_pool_handler_cb  handler;            // user-given handler to process each task dequeued from the queue.
};

typedef struct cx_pool_handler_args_t
{
    cx_pool_t*          pool;               // pool instance.
    uint16_t            index;              // worker number that this current thread manages.
};

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_pool_t*              cx_pool_init(const char* name, uint16_t _numWorkers, cx_pool_handler_cb _taskHandler);

void                    cx_pool_destroy(cx_pool_t* _pool);

void                    cx_pool_resize(cx_pool_t* _pool, uint16_t _numWorkers);

void                    cx_pool_submit(cx_pool_t* _pool, void* _taskData);

uint16_t                cx_pool_size(cx_pool_t* _pool);

#endif // CX_POOL_H_
