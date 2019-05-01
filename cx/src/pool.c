#include "pool.h"
#include "mem.h"
#include "str.h"

#include <errno.h>

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void* _cx_pool_main_loop(void* _args);

static void _cx_pool_pause(cx_pool_t* _pool, bool _block);

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

    pool->mtxPauseInit = (0 == pthread_mutex_init(&pool->mtxPause, NULL));
    CX_CHECK(pool->mtxPauseInit, "pause mutex initialization failed!");

    pool->condPauseInit = (0 == pthread_cond_init(&pool->condPause, NULL));
    CX_CHECK(pool->condPauseInit, "pause cond initialization failed!");

    pool->condPausedInit = (0 == pthread_cond_init(&pool->condPaused, NULL));
    CX_CHECK(pool->condPausedInit, "paused cond initialization failed!");

    if (pool->mtxPauseInit && pool->condPauseInit && pool->condPausedInit)
    {
        pool->queue = cx_mcq_init();
        if (NULL != pool->queue)
        {
            pool->workers = CX_MEM_ARR_ALLOC(pool->workers, _numWorkers);
            if (NULL != pool->workers)
            {
                CX_INFO("[pool: %s] initiating thread pool with %d workers...", pool->name, _numWorkers);
                pool->state = CX_POOL_STATE_RUNNING;

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
    }

    cx_pool_destroy(pool);
    return NULL;
}

void cx_pool_destroy(cx_pool_t* _pool)
{
    if (NULL == _pool) return;

    CX_CHECK(CX_POOL_STATE_RUNNING == _pool->state || CX_POOL_STATE_PAUSED == _pool->state,
        "you cannot destroy pool '%s' in his current state (#%d)!", _pool->name, _pool->state);

    CX_INFO("[pool: %s] terminating thread pool...", _pool->name);

    // submit SHUTDOWN task to each worker to shut it down
    for (uint16_t i = 0; i < _pool->workersCount; i++)
    {
        cx_pool_submit_first(_pool, CX_POOL_TASK_SHUTDOWN);
    }
    
    // resume the workers if they were paused, so that they can consume this new task
    if (CX_POOL_STATE_PAUSED == _pool->state)
    {
        cx_pool_resume(_pool);
    }

    // wait for them to complete
    for (uint16_t i = 0; i < _pool->workersCount; i++)
    {
        pthread_join(_pool->workers[i].thread, NULL);
    }

    CX_INFO("[pool: %s] terminated gracefully with %d tasks pending in the queue.", _pool->name, cx_mcq_size(_pool->queue));
    _pool->state = CX_POOL_STATE_TERMINATED;

    if (_pool->mtxPauseInit)
    {
        pthread_mutex_destroy(&_pool->mtxPause);
        _pool->mtxPauseInit = false;
    }

    if (_pool->condPauseInit)
    {
        pthread_cond_destroy(&_pool->condPause);
        _pool->condPauseInit = false;
    }

    if (_pool->condPausedInit)
    {
        pthread_cond_destroy(&_pool->condPaused);
        _pool->condPausedInit = false;
    }

    cx_mcq_destroy(_pool->queue, NULL);
    free(_pool->workers);
    free(_pool);
}

void cx_pool_submit(cx_pool_t* _pool, void* _taskData)
{
    CX_CHECK_NOT_NULL(_pool);
    cx_mcq_push(_pool->queue, _taskData);
}

void cx_pool_submit_first(cx_pool_t* _pool, void* _taskData)
{
    CX_CHECK_NOT_NULL(_pool);
    cx_mcq_push_first(_pool->queue, _taskData);
}

uint16_t cx_pool_size(cx_pool_t* _pool)
{
    CX_CHECK_NOT_NULL(_pool);
    return _pool->workersCount;
}

bool cx_pool_is_paused(cx_pool_t* _pool)
{
    pthread_mutex_lock(&_pool->mtxPause);
    
    bool result = true
        && CX_POOL_STATE_PAUSED == _pool->state        
        && _pool->workersCount == _pool->workersCountPaused;

    pthread_mutex_unlock(&_pool->mtxPause);
    return result;
}

void cx_pool_pause(cx_pool_t* _pool)
{
    _cx_pool_pause(_pool, true);
}

void cx_pool_pause_nb(cx_pool_t* _pool)
{
    _cx_pool_pause(_pool, false);
}

void cx_pool_resume(cx_pool_t* _pool)
{
    pthread_mutex_lock(&_pool->mtxPause);
    if (CX_POOL_STATE_PAUSED == _pool->state)
    {
        _pool->workersCountPaused = 0;
        _pool->state = CX_POOL_STATE_RUNNING;
        pthread_cond_broadcast(&_pool->condPause);
    }
    else
    {
        CX_CHECK(CX_ALW, "pool '%s' is not in paused state!", _pool->name);
    }
    pthread_mutex_unlock(&_pool->mtxPause);
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static void* _cx_pool_main_loop(void* _args)
{
    CX_CHECK_NOT_NULL(_args);

    cx_pool_handler_args_t* args = _args;
    void* data = NULL;

    CX_INFO("[pool: %s-%03d] main loop started.", args->pool->name, args->index);
    args->pool->workers[args->index].isRunning = true;

    while (true)
    {
        args->pool->workers[args->index].isWaiting = true;
        cx_mcq_pop(args->pool->queue, &data);
        args->pool->workers[args->index].isWaiting = false;

        if (CX_POOL_TASK_SHUTDOWN == data)
        {
            break;
        }
        else if (CX_POOL_TASK_PAUSE == data)
        {
            CX_INFO("[pool: %s-%03d] paused.", args->pool->name, args->index);
            args->pool->workers[args->index].isPaused = true;

            pthread_mutex_lock(&args->pool->mtxPause);
            {
                // let the scheduler (main thread) know we're in paused state now.
                args->pool->workersCountPaused++;
                pthread_cond_signal(&args->pool->condPaused);

                while (CX_POOL_STATE_PAUSED == args->pool->state)
                {
                    pthread_cond_wait(&args->pool->condPause, &args->pool->mtxPause);
                }
            }
            pthread_mutex_unlock(&args->pool->mtxPause);

            CX_INFO("[pool: %s-%03d] resumed.", args->pool->name, args->index);
            args->pool->workers[args->index].isPaused = false;
        }
        else
        {
            CX_INFO("[pool: %s-%03d] handling data %p.", args->pool->name, args->index, data);
            args->pool->handler(data);
        }
    }
    
    CX_INFO("[pool: %s-%03d] terminated gracefully.", args->pool->name, args->index);
    args->pool->workers[args->index].isRunning = false;

    free(_args);
    return NULL;
}

static void _cx_pool_pause(cx_pool_t* _pool, bool _block)
{
    CX_CHECK_NOT_NULL(_pool);

    pthread_mutex_lock(&_pool->mtxPause);
    if (CX_POOL_STATE_PAUSED != _pool->state)
    {
        CX_INFO("[pool: %s] pausing thread pool...", _pool->name);
        _pool->state = CX_POOL_STATE_PAUSED;
        _pool->workersCountPaused = 0;

        // submit PAUSE task to each worker to stop working
        for (uint16_t i = 0; i < _pool->workersCount; i++)
        {
            cx_pool_submit_first(_pool, CX_POOL_TASK_PAUSE);
        }

        if (_block)
        {
            // wait for all the workers to pause.
            while (_pool->workersCountPaused < _pool->workersCount)
            {
                pthread_cond_wait(&_pool->condPaused, &_pool->mtxPause);
            }
            CX_INFO("[pool: %s] thread pool paused with %d tasks pending in the queue.", _pool->name, cx_mcq_size(_pool->queue));
        }
    }
    else
    {
        CX_CHECK(CX_ALW, "pool '%s' is already paused!", _pool->name);
    }
    pthread_mutex_unlock(&_pool->mtxPause);
}