#include "lfs.h"
#include "memtable.h"
#include "worker.h"
#include "fs.h"

#include <ker/cli_parser.h>
#include <ker/cli_reporter.h>

#include <cx/cx.h>
#include <cx/timer.h>
#include <cx/mem.h>
#include <cx/file.h>
#include <cx/str.h>
#include <cx/cli.h>
#include <cx/fs.h>
#include <cx/cdict.h>

#include <lfs/lfs_protocol.h>
#include <commons/config.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

lfs_ctx_t           g_ctx;                                  // global LFS context
static uint16_t     m_auxHandles[MAX_TASKS];  // statically allocated aux buffer for storing handles

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool         logger_init(cx_err_t* _err);
static void         logger_destroy();

static bool         cfg_init(const char* _cfgFilePath, cx_err_t* _err);
static void         cfg_destroy();

static bool         lfs_init(cx_err_t* _err);
static void         lfs_destroy();

static bool         net_init(cx_err_t* _err);
static void         net_destroy();

static bool         cli_init();
static void         cli_destroy();

static void         handle_cli_command(const cx_cli_cmd_t* _cmd);
static bool         handle_timer_tick(uint64_t _expirations, uint32_t _id, void* _userData);
static void         handle_wk_task(task_t* _task);
static bool         handle_mt_task(task_t* _task);

static void         queue_process();

static void         tasks_update();
static void         task_completed(const task_t* _task);
static void         task_free(task_t* _task);

static void         api_response_create(const task_t* _task);
static void         api_response_drop(const task_t* _task);
static void         api_response_describe(const task_t* _task);
static void         api_response_select(const task_t* _task);
static void         api_response_insert(const task_t* _task);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

int main(int _argc, char** _argv)
{
    cx_init(PROJECT_NAME);
    CX_MEM_ZERO(g_ctx);

    cx_err_t err;

    g_ctx.isRunning = true
        && cx_timer_init(MAX_TABLES + 1, handle_timer_tick, &err)
        && logger_init(&err)
        && cfg_init("res/lfs.cfg", &err)
        && lfs_init(&err)
        && net_init(&err)
        && cli_init(&err);

    if (0 == err.code)
    {
        cx_cli_cmd_t* cmd = NULL;
        cx_time_update();

        while (g_ctx.isRunning)
        {
            cx_time_update();
            CX_WARN(cx_time_delta() < 0.1, "server is running slow! timeDelta=%fs", cx_time_delta()); // check we're at least at 10hz/s

            // poll cli events
            if (cx_cli_command_begin(&cmd))
                handle_cli_command(cmd);

            // poll socket events
            cx_net_poll_events(g_ctx.sv);
            
            // update tasks
            tasks_update();

            // poll timer events
            cx_timer_poll_events();

            // process main-thread queue
            queue_process();
        }
    }
    else if (NULL != g_ctx.log)
    {
        log_error(g_ctx.log, "initialization failed (errcode %d). %s", err.code, err.desc);
    }
    else
    {
        printf("[FATAL] (errcode %d) %s", err.code, err.desc);
    }

    CX_INFO("node is shutting down...", PROJECT_NAME);
    cli_destroy();
    net_destroy();
    lfs_destroy();
    cfg_destroy();
    cx_timer_destroy();

    if (0 == err.code)
        CX_INFO("node terminated gracefully.");
    else
        CX_INFO("node terminated with error %d.", err.code);
    
    logger_destroy();
    return err.code;
}

uint16_t lfs_task_create(TASK_ORIGIN _origin, TASK_TYPE _type, void* _data, cx_net_client_t* _client)
{
    pthread_mutex_lock(&g_ctx.tasksMutex);
    uint16_t handle = cx_handle_alloc(g_ctx.tasksHalloc);
    pthread_mutex_unlock(&g_ctx.tasksMutex);

    if (INVALID_HANDLE != handle)
    {
        task_t* task = &(g_ctx.tasks[handle]);
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
  
    return handle;
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static bool logger_init(cx_err_t* _err)
{
    cx_timestamp_t timestamp;
    cx_time_stamp(&timestamp);

    cx_path_t path;
    cx_fs_path(&path, "logs");
    if (cx_fs_mkdir(&path, _err))
    {
        cx_fs_path(&path, "logs/%s.txt", timestamp);
        g_ctx.log = log_create(path, PROJECT_NAME, true, LOG_LEVEL_INFO);

        if (NULL != g_ctx.log)
        {
            CX_INFO("log file: %s", path);
            return true;
        }

        CX_ERR_SET(_err, LFS_ERR_LOGGER_FAILED, "%s log initialization failed (%s).", PROJECT_NAME, path);
    }
    else
    {
        CX_ERR_SET(_err, LFS_ERR_LOGGER_FAILED, "%s logs folder creation failed (%s).", PROJECT_NAME, path);
    }
    return false;
}

static void logger_destroy()
{
    if (NULL != g_ctx.log)
    {
        
        log_destroy(g_ctx.log);
        g_ctx.log = NULL;
    }
}

static bool cfg_init(const char* _cfgFilePath, cx_err_t* _err)
{
    char* temp = NULL;
    char* key = "";

    cx_path_t cfgPath;
    cx_fs_path(&cfgPath, "%s", _cfgFilePath);

    g_ctx.cfg.handle = config_create(cfgPath);
    CX_CHECK_NOT_NULL(g_ctx.cfg.handle);

    if (NULL != g_ctx.cfg.handle)
    {
        CX_INFO("config file: %s", cfgPath);

        key = "listeningIp";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            temp = config_get_string_value(g_ctx.cfg.handle, key);
            cx_str_copy(g_ctx.cfg.listeningIp, sizeof(g_ctx.cfg.listeningIp), temp);
        }
        else
        {
            goto key_missing;
        }

        key = "listeningPort";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.listeningPort = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }
         
        key = "workers";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.workers = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }
         
        key = "rootDirectory";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            temp = config_get_string_value(g_ctx.cfg.handle, key);
            cx_str_copy(g_ctx.cfg.rootDir, sizeof(g_ctx.cfg.rootDir), temp);
        }
        else
        {
            goto key_missing;
        }

        key = "blocksCount";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.blocksCount = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = "blocksSize";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.blocksSize = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = "delay";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.delay = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }
         
        key = "valueSize";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.valueSize = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = "dumpInterval";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.dumpInterval = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

         return true;

     key_missing:
         CX_ERR_SET(_err, LFS_ERR_CFG_MISSINGKEY, "key '%s' is missing in the configuration file.", key);
    }
    else
    {
        CX_ERR_SET(_err, LFS_ERR_CFG_NOTFOUND, "configuration file '%s' is missing or not readable.", cfgPath);
    }

    return false;
}

static void cfg_destroy()
{
    if (NULL != g_ctx.cfg.handle)
    {
        config_destroy(g_ctx.cfg.handle);
        g_ctx.cfg.handle = NULL;
    }
}

static bool lfs_init(cx_err_t* _err)
{
    g_ctx.tasksMutexI = (0 == pthread_mutex_init(&g_ctx.tasksMutex, NULL));
    if (!g_ctx.tasksMutexI)
    {
        CX_ERR_SET(_err, LFS_ERR_INIT_MTX, "tasks mutex creation failed.");
        return false;
    }

    g_ctx.tasksHalloc = cx_halloc_init(MAX_TASKS);
    CX_MEM_ZERO(g_ctx.tasks);
    if (NULL == g_ctx.tasksHalloc)
    {
        CX_ERR_SET(_err, LFS_ERR_INIT_HALLOC, "tasks handle allocator creation failed.");
        return false;
    }
    for (uint32_t i = 0; i < MAX_TASKS; i++)
    {
        g_ctx.tasks[i].handle = i;
        g_ctx.tasks[i].clientHandle = INVALID_HANDLE;
    }

    g_ctx.tablesHalloc = cx_halloc_init(MAX_TABLES);
    CX_MEM_ZERO(g_ctx.tables);
    if (NULL == g_ctx.tablesHalloc)
    {
        CX_ERR_SET(_err, LFS_ERR_INIT_HALLOC, "tables handle allocator creation failed.");
        return false;
    }
    for (uint32_t i = 0; i < MAX_TABLES; i++)
    {
        g_ctx.tables[i].handle = i;
    }

    g_ctx.pool = cx_pool_init("worker", g_ctx.cfg.workers, (cx_pool_handler_cb)handle_wk_task);
    if (NULL == g_ctx.pool)
    {
        CX_ERR_SET(_err, LFS_ERR_INIT_THREADPOOL, "thread pool creation failed.");
        return false;
    }
    
    g_ctx.mtQueue = queue_create();
    if (NULL == g_ctx.mtQueue)
    {
        CX_ERR_SET(_err, LFS_ERR_INIT_QUEUE, "main-thread queue creation failed.");
        return false;
    }

    g_ctx.timerDump = cx_timer_add(g_ctx.cfg.dumpInterval, LFS_TIMER_DUMP, NULL);
    if (INVALID_HANDLE == g_ctx.timerDump)
    {
        CX_ERR_SET(_err, LFS_ERR_INIT_TIMER, "thread pool creation failed.");
        return false;
    }

    return fs_init(_err);
}

static void lfs_destroy()
{
    fs_destroy();

    uint16_t max = 0; 
    uint16_t handle = INVALID_HANDLE;
    task_t* request = NULL;
    table_t* table = NULL;

    if (g_ctx.tasksMutexI)
    {
        pthread_mutex_destroy(&g_ctx.tasksMutex);
        g_ctx.tasksMutexI = false;
    }

    if (NULL != g_ctx.tasksHalloc)
    {
        max = cx_handle_count(g_ctx.tasksHalloc);
        for (uint16_t i = 0; i < max; i++)
        {
            handle = cx_handle_at(g_ctx.tasksHalloc, i);
            request = &(g_ctx.tasks[handle]);
            task_free(request);
        }
        cx_halloc_destroy(g_ctx.tasksHalloc);
        g_ctx.tasksHalloc = NULL;
    }

    if (NULL != g_ctx.tablesHalloc)
    {
        max = cx_handle_count(g_ctx.tablesHalloc);
        for (uint16_t i = 0; i < max; i++)
        {
            handle = cx_handle_at(g_ctx.tablesHalloc, i);
            table = &(g_ctx.tables[handle]);
            fs_table_destroy(table);
        }
        cx_halloc_destroy(g_ctx.tablesHalloc);
        g_ctx.tablesHalloc = NULL;
    }

    if (NULL != g_ctx.pool)
    {
        cx_pool_destroy(g_ctx.pool);
        g_ctx.pool = NULL;
    }

    if (NULL != g_ctx.mtQueue)
    {
        queue_destroy(g_ctx.mtQueue);
        g_ctx.mtQueue = NULL;
    }

    if (INVALID_HANDLE != g_ctx.timerDump)
    {
        cx_timer_remove(g_ctx.timerDump);
        g_ctx.timerDump = INVALID_HANDLE;
    }
}

static bool net_init(cx_err_t* _err)
{
    cx_net_args_t svCtxArgs;
    CX_MEM_ZERO(svCtxArgs);
    cx_str_copy(svCtxArgs.name, sizeof(svCtxArgs.name), "api");
    cx_str_copy(svCtxArgs.ip, sizeof(svCtxArgs.ip), g_ctx.cfg.listeningIp);
    svCtxArgs.port = g_ctx.cfg.listeningPort;

    // message headers to handlers mappings
    svCtxArgs.msgHandlers[LFSP_SUM_REQUEST] = (cx_net_handler_cb*)lfs_handle_sum_request;

    // start server context and start listening for requests
    g_ctx.sv = cx_net_listen(&svCtxArgs);

    if (NULL != g_ctx.sv)
    {
        return true;
    }
    else
    {
        CX_ERR_SET(_err, LFS_ERR_NET_FAILED, "could not start a listening server context on %s:%d.",
            svCtxArgs.ip, svCtxArgs.port);
        return false;
    }
}

static void net_destroy()
{
    cx_net_close(g_ctx.sv);
    g_ctx.sv = NULL;
}

static bool cli_init()
{
    return cx_cli_init();
}

static void cli_destroy()
{
    cx_cli_destroy();
}

static void handle_cli_command(const cx_cli_cmd_t* _cmd)
{
    cx_err_t  err;
    CX_MEM_ZERO(err);

    char*       tableName = NULL;
    uint16_t    key = 0;
    char*       value = NULL;
    uint32_t    timestamp = 0;
    uint8_t     consistency = 0;
    uint16_t    numPartitions = 0;
    uint32_t    compactionInterval = 0;
    uint32_t    packetSize = 0;

    if (strcmp("EXIT", _cmd->header) == 0)
    {
        g_ctx.isRunning = false;
    }
    else if (strcmp("CREATE", _cmd->header) == 0)
    {
        if (cli_parse_create(_cmd, &err, &tableName, &consistency, &numPartitions, &compactionInterval))
        {
            packetSize = lfs_pack_create(g_ctx.buffer, sizeof(g_ctx.buffer), 0, tableName, consistency, numPartitions, compactionInterval);
            lfs_handle_create(g_ctx.sv, NULL, g_ctx.buffer, packetSize);
        }
    }
    else if (strcmp("DROP", _cmd->header) == 0)
    {
        if (cli_parse_drop(_cmd, &err, &tableName))
        {
            packetSize = lfs_pack_drop(g_ctx.buffer, sizeof(g_ctx.buffer), 0, tableName);
            lfs_handle_drop(g_ctx.sv, NULL, g_ctx.buffer, packetSize);
        }
    }
    else if (strcmp("DESCRIBE", _cmd->header) == 0)
    {
        if (cli_parse_describe(_cmd, &err, &tableName))
        {
            packetSize = lfs_pack_describe(g_ctx.buffer, sizeof(g_ctx.buffer), 0, tableName);
            lfs_handle_describe(g_ctx.sv, NULL, g_ctx.buffer, packetSize);
        }
    }
    else if (strcmp("SELECT", _cmd->header) == 0)
    {
        if (cli_parse_select(_cmd, &err, &tableName, &key))
        {
            packetSize = lfs_pack_select(g_ctx.buffer, sizeof(g_ctx.buffer), 0, tableName, key);
            lfs_handle_select(g_ctx.sv, NULL, g_ctx.buffer, packetSize);
        }
    }
    else if (strcmp("INSERT", _cmd->header) == 0)
    {
        if (cli_parse_insert(_cmd, &err, &tableName, &key, &value, &timestamp))
        {
            packetSize = lfs_pack_insert(g_ctx.buffer, sizeof(g_ctx.buffer), 0, tableName, key, value, timestamp);
            lfs_handle_insert(g_ctx.sv, NULL, g_ctx.buffer, packetSize);
        }
    }
    else if (strcmp("PAUSE", _cmd->header) == 0)
    {
        cx_pool_pause(g_ctx.pool);
        cx_cli_command_end();
    }
    else if (strcmp("RESUME", _cmd->header) == 0)
    {
        cx_pool_resume(g_ctx.pool);
        cx_cli_command_end();
    }
    else if (strcmp("DUMP", _cmd->header) == 0)
    {
        cx_err_t err;
        table_t* table;
        if (fs_table_exists(_cmd->args[0], &table))
        {
            if (memtable_make_dump(&table->memtable, &err))
            {
                CX_INFO("memtable dump for table '%s' completed successfully.", _cmd->args[0]);
            }
            else
            {
                CX_INFO("memtable dump for table '%s' failed: %s.", _cmd->args[0], err.desc);
            }
        }
        cx_cli_command_end();
    }
    else
    {
        CX_ERR_SET(&err, 1, "Unknown command '%s'.", _cmd->header);
    }

    if (0 != err.code)
    {
        cli_report_error(&err);
        cx_cli_command_end();
    }
}

static void handle_wk_task(task_t* _task)
{
    _task->state = TASK_STATE_RUNNING;

    switch (_task->type)
    {
    case TASK_WT_CREATE:
        worker_handle_create(_task);
        break;

    case TASK_WT_DROP:
        worker_handle_drop(_task);
        break;

    case TASK_WT_DESCRIBE:
        worker_handle_describe(_task);
        break;

    case TASK_WT_SELECT:
        worker_handle_select(_task);
        break;

    case TASK_WT_INSERT:
        worker_handle_insert(_task);
        break;

    case TASK_WT_DUMP:
        worker_handle_dump(_task);
        break;

    case TASK_WT_COMPACT:
        worker_handle_compact(_task);
        break;

    default:
        CX_WARN(CX_ALW, "undefined <worker-thread> behaviour for task type #%d.", _task->type);
        break;
    }
}

static bool handle_mt_task(task_t* _task)
{
    CX_CHECK_NOT_NULL(_task);
    _task->state = TASK_STATE_RUNNING;

    bool     success = false;
    table_t* table = NULL;
    task_t*  task = NULL;
    uint16_t taskHandle = INVALID_HANDLE;

    switch (_task->type)
    {
    case TASK_MT_FREE:
    {
        table = &g_ctx.tables[_task->tableHandle];
        if (0 == table->operations)
        {
            fs_table_destroy(table);
            cx_handle_free(g_ctx.tablesHalloc, table->handle);
            success = true;
        }
        break;
    }

    case TASK_MT_COMPACT:
    {
        table = &g_ctx.tables[_task->tableHandle];

        if (!table->blocked)
        {
            table->blocked = true;
            table->blockedStartTime = cx_time_counter();
        }

        if (0 == table->operations)
        {
            taskHandle = lfs_task_create(TASK_ORIGIN_INTERNAL, TASK_WT_COMPACT, NULL, NULL);
            if (INVALID_HANDLE != taskHandle)
            {
                task = &(g_ctx.tasks[taskHandle]);

                data_compact_t* data = CX_MEM_STRUCT_ALLOC(data);
                cx_str_copy(data->tableName, sizeof(data->tableName), table->meta.name);

                task->data = data;
                task->state = TASK_STATE_NEW;
                success = true;
            }
            else
            {
                //TODO if this handle allocation fails, we end up with a table blocked indefinitely
            }
        }
        break;
    }

    case TASK_MT_DUMP:
    {
        uint16_t max = cx_handle_count(g_ctx.tablesHalloc);
        uint16_t handle = INVALID_HANDLE;
        data_dump_t* data = NULL;

        for (uint16_t i = 0; i < max; i++)
        {
            handle = cx_handle_at(g_ctx.tablesHalloc, i);
            table = &(g_ctx.tables[handle]);

            if (!table->deleted)
            {
                taskHandle = lfs_task_create(TASK_ORIGIN_INTERNAL, TASK_WT_DUMP, NULL, NULL);
                if (INVALID_HANDLE != taskHandle)
                {
                    task = &(g_ctx.tasks[taskHandle]);

                    data = CX_MEM_STRUCT_ALLOC(data);
                    cx_str_copy(data->tableName, sizeof(data->tableName), table->meta.name);

                    task->data = data;
                    task->state = TASK_STATE_NEW;
                }
            }
        }
        success = true;
        break;
    }

    case TASK_MT_UNBLOCK:
    {
        table = &g_ctx.tables[_task->tableHandle];
        table->blocked = false;

        if (table->justCreated)
        {
            table->justCreated = false;
            table->timerHandle = cx_timer_add(table->meta.compactionInterval, LFS_TIMER_COMPACT, table);
            CX_CHECK(INVALID_HANDLE != table->timerHandle, "we ran out of timer handles for table '%s'!", table->meta.name);
        }

        while (!queue_is_empty(table->blockedQueue))
        {
            task = queue_pop(table->blockedQueue);
            task->state = TASK_STATE_NEW;
        }
        
        success = true;
        break;
    }

    default:
        CX_WARN(CX_ALW, "undefined <main-thread> behaviour for task type #%d.", _task->type);
        break;
    }
    
    return success;
}

static bool handle_timer_tick(uint64_t _expirations, uint32_t _type, void* _userData)
{
    bool stopTimer = false;
    uint16_t taskHandle = INVALID_HANDLE;
    task_t* task = NULL;

    switch (_type)
    {
    case LFS_TIMER_DUMP:
    {
        taskHandle = lfs_task_create(TASK_ORIGIN_INTERNAL, TASK_MT_DUMP, NULL, NULL);
        task = &(g_ctx.tasks[taskHandle]);
        task->state = TASK_STATE_NEW;
        break;
    }

    case LFS_TIMER_COMPACT:
        taskHandle = lfs_task_create(TASK_ORIGIN_INTERNAL, TASK_MT_COMPACT, NULL, NULL);
        task = &(g_ctx.tasks[taskHandle]);
        task->tableHandle = ((table_t*)_userData)->handle;
        task->state = TASK_STATE_NEW;
        break;

    default:
        CX_WARN(CX_ALW, "undefined <tick> behaviour for timer of type #%d.", _type);
        break;
    }

    return !stopTimer;
}

void tasks_update()
{
    uint16_t max = cx_handle_count(g_ctx.tasksHalloc);
    uint16_t handle = INVALID_HANDLE;
    task_t*  task = NULL;
    table_t* table = NULL;
    uint16_t removeCount = 0;

    for (uint16_t i = 0; i < max; i++)
    {
        handle = cx_handle_at(g_ctx.tasksHalloc, i);
        task = &(g_ctx.tasks[handle]);

        if (TASK_STATE_NEW == task->state)
        {
            task->startTime = cx_time_counter();
            CX_MEM_ZERO(task->err);

            if (TASK_WT & task->type)
            {
                cx_pool_submit(g_ctx.pool, task);
            }
            else
            {
                queue_push(g_ctx.mtQueue, task);
            }
        }
        else if (TASK_STATE_COMPLETED == task->state)
        {
            task_completed(task);
            task_free(task);
            m_auxHandles[removeCount++] = handle;
        }
        else if (TASK_STATE_BLOCKED_RESCHEDULE == task->state)
        {
            if (INVALID_HANDLE != task->tableHandle)
            {
                table = &g_ctx.tables[task->tableHandle];
                
                queue_push(table->blockedQueue, task);
                task->state = TASK_STATE_BLOCKED_AWAITING;
            }
        }
    }

    pthread_mutex_lock(&g_ctx.tasksMutex);
    for (uint16_t i = 0; i < removeCount; i++)
    {   
        handle = m_auxHandles[i];
        task = &(g_ctx.tasks[handle]);
        cx_handle_free(g_ctx.tasksHalloc, handle);
    }
    pthread_mutex_unlock(&g_ctx.tasksMutex);
}

static void task_completed(const task_t* _task)
{
    switch (_task->type)
    {
    case TASK_WT_CREATE:
        if (TASK_ORIGIN_API == _task->origin)
            api_response_create(_task);
        else
            cli_report_create(_task);
        break;

    case TASK_WT_DROP:
        if (TASK_ORIGIN_API == _task->origin)
            api_response_drop(_task);
        else
            cli_report_drop(_task);
        break;

    case TASK_WT_DESCRIBE:
        if (TASK_ORIGIN_API == _task->origin)
            api_response_describe(_task);
        else
            cli_report_describe(_task);
        break;

    case TASK_WT_SELECT:
        if (TASK_ORIGIN_API == _task->origin)
            api_response_select(_task);
        else
            cli_report_select(_task);
        break;

    case TASK_WT_INSERT:
        if (TASK_ORIGIN_API == _task->origin)
            api_response_insert(_task);
        else
            cli_report_insert(_task);
        break;

    case TASK_WT_DUMP:
        break;

    case TASK_MT_UNBLOCK:
    {        
        table_t* table = &g_ctx.tables[_task->tableHandle];        
        if (!table->justCreated)
            cli_report_unblocked(table->meta.name, cx_time_counter() - table->blockedStartTime);
        break;
    }

    case TASK_MT_DUMP:
        break;

    case TASK_MT_COMPACT:
        break;

    case TASK_MT_FREE:
        break;

    default:
        CX_WARN(CX_ALW, "undefined <completed> behaviour for request type #%d.", _task->type);
        break;
    }

    if (TASK_ORIGIN_CLI == _task->origin)
    {
        // mark the current (processed) command as done
        cx_cli_command_end();
    }
}

static void task_free(task_t* _task)
{
    switch (_task->type)
    {
    case TASK_WT_CREATE:
    {
        data_create_t* data = (data_create_t*)_task->data;
        break;
    }

    case TASK_WT_DROP:
    {
        data_drop_t* data = (data_drop_t*)_task->data;
        break;
    }

    case TASK_WT_DESCRIBE:
    {
        data_describe_t* data = (data_describe_t*)_task->data;
        free(data->tables);
        data->tables = NULL;
        data->tablesCount = 0;
        break;
    }

    case TASK_WT_SELECT:
    {
        data_select_t* data = (data_select_t*)_task->data;
        free(data->record.value);
        data->record.value = NULL;
        break;
    }

    case TASK_WT_INSERT:
    {
        data_insert_t* data = (data_insert_t*)_task->data;
        free(data->record.value);
        data->record.value = NULL;
        break;
    }

    case TASK_WT_DUMP:
    {
        data_dump_t* data = (data_dump_t*)_task->data;
        break;
    }

    case TASK_WT_COMPACT:
    {
        data_compact_t* data = (data_compact_t*)_task->data;
        break;
    }

    case TASK_MT_FREE:
    {
        break;
    }

    case TASK_MT_COMPACT:
    {
        break;
    }

    case TASK_MT_DUMP:
    {
        break;
    }

    case TASK_MT_UNBLOCK:
    {
        break;
    }

    default:
        CX_WARN(CX_ALW, "undefined <free> behaviour for request type #%d.", _task->type);
        break;
    }

    free(_task->data);
    
    // note that requests are statically allocated. we don't want to free them
    // here since we'll keep reusing them in subsequent requests.
    _task->clientHandle = INVALID_HANDLE;
    _task->state = TASK_STATE_NONE;
    _task->origin = TASK_ORIGIN_NONE;
    _task->type = TASK_TYPE_NONE;
    _task->data = NULL;
}

static void queue_process()
{
    task_t* task = NULL;
    uint32_t count = 0;

    uint32_t max = queue_size(g_ctx.mtQueue);
    if (0 >= max) return;

    task = queue_pop(g_ctx.mtQueue);
    while (count < max)
    {
        if (handle_mt_task(task))
        {
            task->state = TASK_STATE_COMPLETED;
        }
        else
        {
            // we can't process it at this time, push it again to our queue
            task->state = TASK_STATE_READY;
            queue_push(g_ctx.mtQueue, task);
        }

        count++;
        task = queue_pop(g_ctx.mtQueue);
    }
}

static void api_response_create(const task_t* _task)
{
    //TODO. reply back to the MEM node that requested this query.
}

static void api_response_drop(const task_t* _task)
{
    //TODO. reply back to the MEM node that requested this query.
}

static void api_response_describe(const task_t* _task)
{
    //TODO. reply back to the MEM node that requested this query.
}

static void api_response_select(const task_t* _task)
{
    //TODO. reply back to the MEM node that requested this query.
}

static void api_response_insert(const task_t* _task)
{
    //TODO. reply back to the MEM node that requested this query.
}