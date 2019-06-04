#ifndef TASKMAN_H_
#define TASKMAN_H_

#include <cx/net.h>
#include <cx/cx.h>
#include <cx/pool.h>

#include "defines.h"

#include <stdint.h>

typedef enum TASK_ORIGIN
{
    TASK_ORIGIN_NONE = 0,           // default, unasigned state.
    TASK_ORIGIN_CLI,                // the origin of this task is the command line interface.
    TASK_ORIGIN_API,                // the origin of this task is a client connected to our server.
    TASK_ORIGIN_INTERNAL,           // the origin of this task is either a timer, our main thread or a worker thread.
} TASK_ORIGIN;

typedef enum TASK_STATE
{
    TASK_STATE_NONE = 0,            // default, unasigned state.
    TASK_STATE_NEW,                 // the task was just created, but it's not yet assigned to the primary queue.
    TASK_STATE_READY,               // the task is ready to be executed and waiting in the queue.
    TASK_STATE_RUNNING,             // the task is being processed by a worker thread.
    TASK_STATE_RUNNING_AWAITING,    // the task is being processed by a worker thread, but it's awaiting for some other node's reply.
    TASK_STATE_COMPLETED,           // the task is completed. check c->err to handle errors (if any).
    TASK_STATE_BLOCKED_RESCHEDULE,  // the task cannot be performed at this time since the table is blocked and it must be re-rescheduled.
    TASK_STATE_BLOCKED_AWAITING,    // the task is awaiting in a table's blocked queue. as soon as the table is unblocked it will be moved to the main queue.
} TASK_STATE;

typedef enum TASK_TYPE
{
    TASK_TYPE_NONE = 0,             // default, unasigned state.
    TASK_MT = UINT8_C(0x0),         // main thread task bitflag.
    TASK_WT = UINT8_C(0x80),        // worker thread task bitflag.
    // Main-thread tasks ----------------------------------------------------------------
    TASK_MT_COMPACT =   TASK_MT | UINT8_C(01), // main thread task to compact a table when it's no longer in use.
    TASK_MT_DUMP =      TASK_MT | UINT8_C(02), // main thread task to dump all the existing tables.
    TASK_MT_FREE =      TASK_MT | UINT8_C(03), // main thread task to free a resource when it's no longer in use.
    // Worker-thread tasks --------------------------------------------------------------
    TASK_WT_CREATE =    TASK_WT | UINT8_C(01), // worker thread task to create a table.
    TASK_WT_DROP =      TASK_WT | UINT8_C(02), // worker thread task to drop a table.
    TASK_WT_DESCRIBE =  TASK_WT | UINT8_C(03), // worker thread task to describe single/multiple table/s.
    TASK_WT_SELECT =    TASK_WT | UINT8_C(04), // worker thread task to do a select query on a table.
    TASK_WT_INSERT =    TASK_WT | UINT8_C(05), // worker thread task to do a single/bulk insert query in a table.
    TASK_WT_DUMP =      TASK_WT | UINT8_C(06), // worker thread task to dump a table.
    TASK_WT_COMPACT =   TASK_WT | UINT8_C(07), // worker thread task to compact a table.
} TASK_TYPE;

typedef struct task_t
{
    uint16_t            handle;                 // handle of this task entry in the tasks container (index).
    double              startTime;              // time counter value of when this task started executing.
    uint32_t            completionKey;          // an auto-incremented number assigned when this task finishes executing for sorting completed tasks in tasks_update();
    TASK_STATE          state;                  // the current state of our task.
    TASK_ORIGIN         origin;                 // origin of this task. it can be either command line interface or sockets api.
    TASK_TYPE           type;                   // the requested operation.
    cx_err_t            err;                    // if the task failed, err contains the error number and the error description for logging purposes.
    uint16_t            clientHandle;           // the handle to the client which requested this task in our server context. INVALID_HANDLE means a CLI-issued task.
    uint16_t            remoteId;               // the remote identifier for this remote task/request. (only if origin is TASK_ORIGIN_API)
    void*               data;                   // the data (arguments and results) of the requested operation. see data_*_t structures in ker/defines.h.
    void*               table;                  // temp variable for specific tasks which operate on a specific table.
    // ker/mem data -----------------------------------------------------------------------------------------------------------------------------------------------
#if defined(MEM) || defined(KER)
    pthread_mutex_t     responseMtx;            // mutex for protecting cond signaling.
    pthread_cond_t      responseCond;           // condition to signal the worker thread to wake up when the response of the request is available.
#endif
} task_t;

typedef bool(*taskman_cb)(task_t* _task);
typedef bool(*taskman_func_cb)(task_t* _task, void* _userData);

typedef struct taskman_ctx_t
{
    bool                isRunning;                                  // true if the taskman is alive and running.
    taskman_cb          taskRunMt;                                  // callback for executing main-thread tasks.
    taskman_cb          taskRunWk;                                  // callback for executing worker-thread tasks.
    taskman_cb          taskCompleted;                              // callback executed after task completion.
    taskman_cb          taskFree;                                   // callback executed during task destruction.
    taskman_cb          taskReschedule;                             // callback for re-scheduling tasks in TASK_STATE_BLOCKED_RESCHEDULE state.
    cx_pool_t*          pool;                                       // main pool of worker threads to process incoming requests.
    t_queue*            mtQueue;                                    // main-thread queue with tasks of type TASK_MT_*.
    task_t              tasks[MAX_TASKS];                           // container for storing incoming tasks during ready/running/completed states.
    cx_handle_alloc_t*  halloc;                                     // handle allocator for tasks container.
    pthread_mutex_t     mtx;                                        // mutex for syncing tasks handle alloc/free.
    bool                mtxInitialized;                             // true if tasksMutex was successfully initialized.
    uint32_t            completionKeyLast;                          // auto-incremental number for sorting completed tasks.
    pthread_mutex_t     completionKeyMtx;                           // mutex for protecting tasksCompletionKeyLast.
    bool                completionKeyMtxInitialized;                // true if taskscompletionKeyMtx was successfully initialized.
    uint16_t            auxHandles[MAX_TASKS];                      // statically allocated aux buffer for storing handles
} taskman_ctx_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool        taskman_init(uint16_t _numWorkers, taskman_cb _runMt, taskman_cb _runWk, taskman_cb _completed, taskman_cb _free, taskman_cb _reschedule, cx_err_t* _err);

void        taskman_destroy();

task_t*     taskman_create(TASK_ORIGIN _origin, TASK_TYPE _type, void* _data, cx_net_client_t* _client);

void        taskman_update();

void        taskman_foreach(taskman_func_cb _func, void* _userData);

void        taskman_completion(task_t* _task);

task_t*     taskman_get(uint16_t _taskHandle);

#endif // TASKMAN_H
