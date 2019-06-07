#ifndef CX_POOL_H_
#define CX_POOL_H_

#include "mcq.h"

#include <stdint.h>
#include <pthread.h>

typedef struct cx_pool_worker_t cx_pool_worker_t;
typedef struct cx_pool_t cx_pool_t;
typedef struct cx_pool_handler_args_t cx_pool_handler_args_t;

typedef void(*cx_pool_handler_cb)(void* _data);

#define CX_POOL_TASK_SHUTDOWN (void*)0x0
#define CX_POOL_TASK_PAUSE (void*)0x1

typedef enum CX_POOL_STATE
{
    CX_POOL_STATE_NONE = 0,                 // the pool is not yet initialized.
    CX_POOL_STATE_RUNNING,                  // the pool is running and processing tasks.
    CX_POOL_STATE_PAUSED,                   // the pool is paused and awaiting to be resumed. (none of its workers is running)
    CX_POOL_STATE_TERMINATED                // the pool is no longer running. (terminated)
} CX_POOL_STATE;

typedef struct cx_pool_worker_t
{
    uint16_t            index;              // worker identifier.
    pthread_t           thread;             // pthread instances for worker.
    bool                isRunning;          // true if this worker is currently running. false if it returned from the main loop.
    bool                isWaiting;          // true if this worker is currently idle waiting for more work.
    bool                isPaused;           // true if this worker is currently paused.
} cx_pool_worker_t;

typedef struct cx_pool_t
{
    CX_POOL_STATE       state;              // current state of the pool.
    char                name[32];           // descriptive context name for debugging purposes.
    cx_pool_worker_t*   workers;            // container for workers of our thread pool.
    uint16_t            workersCount;       // number of workers in our thread pool.    
    uint16_t            workersCountPaused; // number of workers in our thread pool in paused state.
    cx_mcq_t*           queue;              // multi-consumer queue which holds the tasks available to the pool.
    cx_pool_handler_cb  handler;            // user-given handler to process each task dequeued from the queue.
    pthread_mutex_t     mtxPause;           // mutex for syncing pauses.
    pthread_cond_t      condPause;          // condition for workers to wait during pauses until the pool is resumed.
    pthread_cond_t      condPaused;         // condition for main thread to wait during the pause process until the pool is fully paused.
    bool                mtxPauseInit;       // true if mtxPause was successfully initialized.
    bool                condPauseInit;      // true if condPause was successfully initialized.
    bool                condPausedInit;     // true if condPaused was successfully initialized.
    pthread_barrier_t   barrier;            // barrier to wait until all the threads are successfully initialized.
} cx_pool_t;

typedef struct cx_pool_handler_args_t
{
    cx_pool_t*          pool;               // pool instance.
    uint16_t            index;              // worker number that this current thread manages.
} cx_pool_handler_args_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_pool_t*              cx_pool_init(const char* name, uint16_t _numWorkers, cx_pool_handler_cb _taskHandler);

void                    cx_pool_destroy(cx_pool_t* _pool);

void                    cx_pool_submit(cx_pool_t* _pool, void* _taskData);

void                    cx_pool_submit_first(cx_pool_t* _pool, void* _taskData);

uint16_t                cx_pool_size(cx_pool_t* _pool);

bool                    cx_pool_is_paused(cx_pool_t* _pool);

void                    cx_pool_pause(cx_pool_t* _pool);

void                    cx_pool_pause_nb(cx_pool_t* _pool);

void                    cx_pool_resume(cx_pool_t* _pool);

#endif // CX_POOL_H_
