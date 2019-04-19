#include "pool.h"
#include "mem.h"
#include "str.h"

#include <errno.h>

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void* _cx_pool_main_loop(void* _args);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_pool_t* cx_pool_init(const char* name, uint16_t _numWorkers, cx_pool_handler_cb _taskHandler)
{
    CX_CHECK(_numWorkers > 0, "_numWorkers must be greater than zero!");
    if (0 >= _numWorkers) return NULL;

    cx_pool_t* pool = CX_MEM_STRUCT_ALLOC(pool);
    cx_pool_handler_args_t* args = NULL;
    bool failed = false;

    cx_str_copy(pool->name, sizeof(pool->name), name);
    pool->handler = _taskHandler;

    pool->queue = cx_mcq_init();
    if (NULL != pool->queue)
    {
        pool->workers = CX_MEM_ARR_ALLOC(pool->workers, _numWorkers);

        if (NULL != pool->workers)
        {
            CX_INFO("[pool: %s] initiating thread pool with %d workers...", pool->name, _numWorkers);
            pool->isRunning = true;

            for (uint16_t i = 0; i < _numWorkers; i++)
            {
                args = CX_MEM_STRUCT_ALLOC(args);
                args->pool = pool;
                args->index = i;

                if (0 != pthread_create(&(pool->workers[i].thread), NULL, _cx_pool_main_loop, args))
                {
                    CX_CHECK(CX_ALW, "[pool: %s] thread #%d creation failed. %s.", pool->name, i, strerror(errno));
                    free(args);
                    failed = true;
                    break;
                }
                pool->workers[i].index = i;
                pool->workersCount++;
            }

            if (!failed) return pool;
        }
    }

    cx_pool_destroy(pool);
    return NULL;
}

void cx_pool_destroy(cx_pool_t* _pool)
{
    if (NULL == _pool) return;

    if (_pool->isRunning)
    {
        CX_INFO("[pool: %s] terminating thread pool...", _pool->name);

        // submit NULL task to each worker to shut it down
        for (uint16_t i = 0; i < _pool->workersCount; i++)
        {
            cx_mcq_push_first(_pool->queue, NULL);
        }

        // wait for them to complete
        for (uint16_t i = 0; i < _pool->workersCount; i++)
        {
            pthread_join(_pool->workers[i].thread, NULL);
        }

        CX_INFO("[pool: %s] terminated successfully with %d tasks pending in the queue.", _pool->name, cx_mcq_size(_pool->queue));
        _pool->isRunning = false;
    }

    cx_mcq_destroy(_pool->queue, NULL);
    free(_pool->workers);
    free(_pool);
}

void cx_pool_resize(cx_pool_t* _pool, uint16_t _numWorkers)
{
    CX_CHECK_NOT_NULL(_pool);
}

void cx_pool_submit(cx_pool_t* _pool, void* _taskData)
{
    CX_CHECK_NOT_NULL(_pool);
    cx_mcq_push(_pool->queue, _taskData);
}

uint16_t cx_pool_size(cx_pool_t* _pool)
{
    CX_CHECK_NOT_NULL(_pool);
    _pool->workersCount;
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static void* _cx_pool_main_loop(void* _args)
{
    CX_CHECK_NOT_NULL(_args);

    cx_pool_handler_args_t* args = _args;
    void* data = NULL;

    args->pool->workers[args->index].isRunning = true;
    CX_INFO("[pool: %s-%03d] main loop started.", args->pool->name, args->index);

    while (true)
    {
        args->pool->workers[args->index].isWaiting = true;
        cx_mcq_pop(args->pool->queue, &data);
        args->pool->workers[args->index].isWaiting = false;

        if (NULL != data)
        {
            args->pool->handler(data);
        }
        else
        {
            break;
        }
    }
    
    CX_INFO("[pool: %s-%03d] terminated gracefully.", args->pool->name, args->index);
    args->pool->workers[args->index].isRunning = false;

    free(_args);
    return NULL;
}