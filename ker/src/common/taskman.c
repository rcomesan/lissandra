#include <ker/taskman.h>

#include <cx/mem.h>
#include <cx/sort.h>
#include <cx/timer.h>

static taskman_ctx_t       m_taskmanCtx;

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void     _taskman_mtqueue_process();

static int32_t  _taskman_comparator(const void* _a, const void* _b, void* _userData);

static void     _taskman_free_task(task_t* _task);

/****************************************************************************************
 ***  PUBLIC DECLARATIONS
 ***************************************************************************************/

bool taskman_init(uint16_t _numWorkers, taskman_cb _runMt, taskman_cb _runWk, taskman_cb _completed, taskman_cb _free, taskman_cb _reschedule, cx_err_t* _err)
{
    m_taskmanCtx.taskRunMt = _runMt;
    m_taskmanCtx.taskRunWk = _runWk;
    m_taskmanCtx.taskCompleted = _completed;
    m_taskmanCtx.taskFree = _free;
    m_taskmanCtx.taskReschedule = _reschedule;

    // make this mutex reentrant, so that we can safely insert tasks inside tasks update loop
    pthread_mutexattr_t attr;
    m_taskmanCtx.mtxInitialized = true
        && (0 == pthread_mutexattr_init(&attr))
        && (0 == pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE))
        && (0 == pthread_mutex_init(&m_taskmanCtx.mtx, &attr));
    pthread_mutexattr_destroy(&attr);
    if (!m_taskmanCtx.mtxInitialized)
    {
        CX_ERR_SET(_err, 1, "taskman mutex creation failed.");
        return false;
    }

    m_taskmanCtx.halloc = cx_halloc_init(MAX_TASKS);
    CX_MEM_ZERO(m_taskmanCtx.tasks);
    if (NULL == m_taskmanCtx.halloc)
    {
        CX_ERR_SET(_err, 1, "taskman handle allocator creation failed.");
        return false;
    }
    for (uint32_t i = 0; i < MAX_TASKS; i++)
    {
        m_taskmanCtx.tasks[i].handle = i;
        m_taskmanCtx.tasks[i].clientHandle = INVALID_HANDLE;
#if defined(MEM) || defined(KER)
        pthread_mutex_init(&m_taskmanCtx.tasks[i].responseMtx, NULL);
        pthread_cond_init(&m_taskmanCtx.tasks[i].responseCond, NULL);
#endif
    }

    m_taskmanCtx.completionKeyLast = 0;
    m_taskmanCtx.completionKeyMtxInitialized = (0 == pthread_mutex_init(&m_taskmanCtx.completionKeyMtx, NULL));
    if (!m_taskmanCtx.completionKeyMtxInitialized)
    {
        CX_ERR_SET(_err, 1, "taskman completionKeyMtx creation failed.");
        return false;
    }

    m_taskmanCtx.pool = cx_pool_init("worker", _numWorkers, (cx_pool_handler_cb)m_taskmanCtx.taskRunWk);
    if (NULL == m_taskmanCtx.pool)
    {
        CX_ERR_SET(_err, 1, "thread pool creation failed.");
        return false;
    }

    m_taskmanCtx.mtQueue = queue_create();
    if (NULL == m_taskmanCtx.mtQueue)
    {
        CX_ERR_SET(_err, 1, "main-thread queue creation failed.");
        return false;
    }

    m_taskmanCtx.isRunning = true;
    return true;
}

void taskman_destroy()
{
    uint16_t max;
    uint16_t handle = INVALID_HANDLE;
    task_t* task = NULL;
    
    m_taskmanCtx.isRunning = false;

    if (NULL != m_taskmanCtx.pool)
    {
        cx_pool_destroy(m_taskmanCtx.pool);
        m_taskmanCtx.pool = NULL;
        // process the few latest completed tasks (if any)
        taskman_update();
    }

    if (NULL != m_taskmanCtx.mtQueue)
    {
        queue_destroy(m_taskmanCtx.mtQueue);
        m_taskmanCtx.mtQueue = NULL;
    }  

    if (m_taskmanCtx.mtxInitialized)
    {
        pthread_mutex_destroy(&m_taskmanCtx.mtx);
        m_taskmanCtx.mtxInitialized = false;
    }

    if (NULL != m_taskmanCtx.halloc)
    {
        max = cx_handle_count(m_taskmanCtx.halloc);
        for (uint16_t i = 0; i < max; i++)
        {
            handle = cx_handle_at(m_taskmanCtx.halloc, i);
            task = &(m_taskmanCtx.tasks[handle]);
            _taskman_free_task(task);

#if defined(MEM) || defined(KER)
            pthread_mutex_destroy(&m_taskmanCtx.tasks[i].responseMtx);
            pthread_cond_destroy(&m_taskmanCtx.tasks[i].responseCond);
#endif
        }
        cx_halloc_destroy(m_taskmanCtx.halloc);
        m_taskmanCtx.halloc = NULL;
    }

    if (m_taskmanCtx.completionKeyMtxInitialized)
    {
        pthread_mutex_destroy(&m_taskmanCtx.completionKeyMtx);
        m_taskmanCtx.completionKeyMtxInitialized = false;
    }
}

task_t* taskman_create(TASK_ORIGIN _origin, TASK_TYPE _type, void* _data, cx_net_client_t* _client)
{
    if (!m_taskmanCtx.isRunning) return NULL;

    pthread_mutex_lock(&m_taskmanCtx.mtx);
    uint16_t handle = cx_handle_alloc(m_taskmanCtx.halloc);
    pthread_mutex_unlock(&m_taskmanCtx.mtx);

    if (INVALID_HANDLE != handle)
    {
        task_t* task = &(m_taskmanCtx.tasks[handle]);
        task->state = TASK_STATE_NONE;
        task->type = _type;
        task->origin = _origin;
        task->data = _data;

        if (TASK_ORIGIN_API == task->origin)
        {
            task->clientHandle = _client->handle;
        }
        else
        {
            task->clientHandle = INVALID_HANDLE;
        }
    }
    CX_CHECK(INVALID_HANDLE != handle, "we ran out of task handles! (ignored task type %d)", _type);

    return INVALID_HANDLE != handle 
        ? &m_taskmanCtx.tasks[handle]
        : NULL;
}

void taskman_update()
{
    uint16_t max = cx_handle_count(m_taskmanCtx.halloc);
    uint16_t handle = INVALID_HANDLE;
    task_t*  task = NULL;
    uint16_t completedCount = 0;

    for (uint16_t i = 0; i < max; i++)
    {
        handle = cx_handle_at(m_taskmanCtx.halloc, i);
        task = &(m_taskmanCtx.tasks[handle]);

        if (m_taskmanCtx.isRunning)
        {
            if (TASK_STATE_NEW == task->state)
            {
                task->state = TASK_STATE_READY;
                task->startTime = cx_time_counter();

                if (TASK_WT & task->type)
                {
                    cx_pool_submit(m_taskmanCtx.pool, task);
                }
                else
                {
                    queue_push(m_taskmanCtx.mtQueue, task);
                }
            }
            else if (TASK_STATE_BLOCKED_RESCHEDULE == task->state)
            {
                m_taskmanCtx.taskReschedule(task);
            }
        }

        if (TASK_STATE_COMPLETED == task->state)
        {
            m_taskmanCtx.auxHandles[completedCount++] = handle;
        }
    }

    if (completedCount > 0)
    {
        pthread_mutex_lock(&m_taskmanCtx.mtx);
        cx_sort_quick(m_taskmanCtx.auxHandles, sizeof(m_taskmanCtx.auxHandles[0]), completedCount, (cx_sort_comp_cb)_taskman_comparator, NULL);

        for (uint16_t i = 0; i < completedCount; i++)
        {
            handle = m_taskmanCtx.auxHandles[i];

            task = &(m_taskmanCtx.tasks[handle]);

            // notify task completion
            m_taskmanCtx.taskCompleted(task);

            // free the task
            _taskman_free_task(task);
        }
        pthread_mutex_unlock(&m_taskmanCtx.mtx);
    }

    if (m_taskmanCtx.isRunning)
        _taskman_mtqueue_process();
}

void taskman_foreach(taskman_func_cb _func, void* _userData)
{
    if (!m_taskmanCtx.isRunning) return;

    uint16_t max = cx_handle_count(m_taskmanCtx.halloc);
    uint16_t handle = INVALID_HANDLE;
    task_t*  task = NULL;

    for (uint16_t i = 0; i < max; i++)
    {
        handle = cx_handle_at(m_taskmanCtx.halloc, i);
        task = &(m_taskmanCtx.tasks[handle]);

        if (!_func(task, _userData))
            return;
    }
}

void taskman_completion(task_t* _task)
{
    if (!m_taskmanCtx.isRunning) return;

    pthread_mutex_lock(&m_taskmanCtx.completionKeyMtx);
    _task->completionKey = m_taskmanCtx.completionKeyLast++;
    _task->state = TASK_STATE_COMPLETED;
    pthread_mutex_unlock(&m_taskmanCtx.completionKeyMtx);
}

task_t* taskman_get(uint16_t _taskHandle)
{
    if (!m_taskmanCtx.isRunning) return NULL;

    if (cx_handle_is_valid(m_taskmanCtx.halloc, _taskHandle))
    {
        return &m_taskmanCtx.tasks[_taskHandle];
    }
    return NULL;
}

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void _taskman_mtqueue_process()
{
    task_t* task = NULL;
    uint32_t count = 0;

    uint32_t max = queue_size(m_taskmanCtx.mtQueue);
    if (0 >= max) return;

    task = queue_pop(m_taskmanCtx.mtQueue);
    while (count < max)
    {
        if (m_taskmanCtx.taskRunMt(task))
        {
            task->state = TASK_STATE_COMPLETED;
        }
        else
        {
            // we can't process it at this time, push it again to our queue
            task->state = TASK_STATE_NEW;
        }

        count++;
        task = queue_pop(m_taskmanCtx.mtQueue);
    }
}

static int32_t _taskman_comparator(const void* _a, const void* _b, void* _userData)
{
    task_t* a = &m_taskmanCtx.tasks[*((uint16_t*)_a)];
    task_t* b = &m_taskmanCtx.tasks[*((uint16_t*)_b)];
    return a->completionKey < b->completionKey ? -1 : 1;
}

static void _taskman_free_task(task_t* _task)
{
    // free task user data
    m_taskmanCtx.taskFree(_task);

    free(_task->data);
    _task->data = NULL;
    _task->table = NULL;

    // note that requests are statically allocated. we don't want to free them
    // here since we'll keep reusing them in subsequent requests.
    _task->startTime = 0;
    _task->completionKey = 0;
    _task->state = TASK_STATE_NONE;
    _task->origin = TASK_ORIGIN_NONE;
    _task->type = TASK_TYPE_NONE;
    _task->clientHandle = INVALID_HANDLE;
    _task->remoteId = 0;
    CX_ERR_CLEAR(&_task->err);
    
    cx_handle_free(m_taskmanCtx.halloc, _task->handle);
}