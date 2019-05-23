#include "lfs.h"
#include "memtable.h"
#include "worker.h"
#include "fs.h"

#include <ker/cli_parser.h>
#include <ker/cli_reporter.h>
#include <ker/taskman.h>

#include <cx/cx.h>
#include <cx/timer.h>
#include <cx/mem.h>
#include <cx/file.h>
#include <cx/str.h>
#include <cx/cli.h>
#include <cx/file.h>
#include <cx/cdict.h>
#include <cx/sort.h>

#include <lfs/lfs_protocol.h>
#include <commons/config.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

lfs_ctx_t           g_ctx;                                  // global LFS context

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

static bool         task_run_mt(task_t* _task);
static bool         task_run_wk(task_t* _task);
static bool         task_completed(task_t* _task);
static bool         task_free(task_t* _task);
static bool         task_reschedule(task_t* _task);

static void         api_response_create(const task_t* _task);
static void         api_response_drop(const task_t* _task);
static void         api_response_describe(const task_t* _task);
static void         api_response_select(const task_t* _task);
static void         api_response_insert(const task_t* _task);

static void         table_unblock(table_t* _table);
static void         table_free(table_t* _table);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

int main(int _argc, char** _argv)
{
    cx_init(PROJECT_NAME);
    CX_MEM_ZERO(g_ctx);

    double timeCounter = 0;
    double timeCounterPrev = 0;
    double timeDelta = 0;

    cx_err_t err;

    g_ctx.isRunning = true
        && cx_timer_init(MAX_TABLES + 1, handle_timer_tick, &err)
        && logger_init(&err)
        && cfg_init("res/lfs.cfg", &err)
        && taskman_init(g_ctx.cfg.workers, task_run_mt, task_run_wk, task_completed, task_free, task_reschedule, &err)
        && lfs_init(&err)
        && net_init(&err)
        && cli_init(&err);

    if (0 == err.code)
    {
        cx_cli_cmd_t* cmd = NULL;
        timeCounterPrev = cx_time_counter();

        while (g_ctx.isRunning)
        {
            timeCounter = cx_time_counter();
            timeDelta = timeCounter - timeCounterPrev;
            timeCounterPrev = timeCounter;
            CX_WARN(timeDelta < 0.1, "server is running slow! timeDelta=%fs", timeDelta); // check we're at least at 10hz/s

            // poll cli events
            if (cx_cli_command_begin(&cmd))
                handle_cli_command(cmd);

            // poll socket events
            cx_net_poll_events(g_ctx.sv);
            
            // poll timer events
            cx_timer_poll_events();

            // update tasks
            taskman_update();

            // pass control back to the os scheduler
            sleep(0);
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
    taskman_destroy();
    cfg_destroy();
    cx_timer_destroy();

    if (0 == err.code)
        CX_INFO("node terminated gracefully.");
    else
        CX_INFO("node terminated with error %d.", err.code);
    
    logger_destroy();
    return err.code;
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static bool logger_init(cx_err_t* _err)
{
    cx_timestamp_t timestamp;
    cx_time_stamp(&timestamp);

    cx_path_t path;
    cx_file_path(&path, "logs");
    if (cx_file_mkdir(&path, _err))
    {
        cx_file_path(&path, "logs/%s.txt", timestamp);
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
    cx_file_path(&cfgPath, "%s", _cfgFilePath);

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
    table_t* table = NULL;

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

static bool handle_timer_tick(uint64_t _expirations, uint32_t _type, void* _userData)
{
    bool stopTimer = false;
    task_t* task = NULL;

    switch (_type)
    {
    case LFS_TIMER_DUMP:
    {
        task = taskman_create(TASK_ORIGIN_INTERNAL, TASK_MT_DUMP, NULL, NULL);
        if (NULL != task)
        {
            task->state = TASK_STATE_NEW;
        }
        break;
    }

    case LFS_TIMER_COMPACT:
    {
        task = taskman_create(TASK_ORIGIN_INTERNAL, TASK_MT_COMPACT, NULL, NULL);
        if (NULL != task)
        {
            task->tableHandle = ((table_t*)_userData)->handle;
            task->state = TASK_STATE_NEW;
        }
        break;
    }

    default:
        CX_WARN(CX_ALW, "undefined <tick> behaviour for timer of type #%d.", _type);
        break;
    }

    return !stopTimer;
}

static void table_unblock(table_t* _table)
{
    CX_CHECK(_table->blocked, "Table '%s' is not blocked!", _table->meta.name);

    pthread_mutex_lock(&_table->mtxOperations);
    _table->blocked = false;

    task_t* task = NULL;
    while (!queue_is_empty(_table->blockedQueue))
    {
        task = queue_pop(_table->blockedQueue);
        task->state = TASK_STATE_NEW;
    }
    pthread_mutex_unlock(&_table->mtxOperations);
}

static void table_free(table_t* _table)
{
    fs_table_destroy(_table);
    cx_handle_free(g_ctx.tablesHalloc, _table->handle);
}

static bool task_run_wk(task_t* _task)
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

    return true;
}

static bool task_run_mt(task_t* _task)
{
    CX_CHECK_NOT_NULL(_task);
    _task->state = TASK_STATE_RUNNING;

    bool     success = false;
    table_t* table = NULL;
    task_t*  task = NULL;

    switch (_task->type)
    {
    case TASK_MT_COMPACT:
    {
        table = &g_ctx.tables[_task->tableHandle];

        if (table->inUse)
        {
            if (!table->compacting)
            {
                pthread_mutex_lock(&table->mtxOperations);
                if (!table->blocked)
                {
                    table->blocked = true;
                    table->blockedStartTime = cx_time_counter();
                }

                if (0 == table->operations)
                {
                    // at this point, our table is blocked (so new operations on it are being blocked and delayed)
                    // and also there're zero pending operations on it. we can safely start compacting it now.
                    table->compacting = true;
                    pthread_mutex_unlock(&table->mtxOperations);

                    task = taskman_create(TASK_ORIGIN_INTERNAL, TASK_WT_COMPACT, NULL, NULL);
                    if (NULL != task)
                    {
                        data_compact_t* data = CX_MEM_STRUCT_ALLOC(data);
                        cx_str_copy(data->tableName, sizeof(data->tableName), table->meta.name);

                        task->data = data;
                        task->tableHandle = table->handle;
                        task->state = TASK_STATE_NEW;
                        success = true;
                    }
                    else
                    {
                        table_unblock(table);
                    }
                }
                else
                {
                    pthread_mutex_unlock(&table->mtxOperations);
                }
            }
            else
            {
                // we can safely ignore this one
                // we don't really want multiple compactions to be performed at the same time
                CX_INFO("Ignoring compaction for table '%s' (another thread is already compacting it).", table->meta.name);
                success = true;
            }
        }
        else
        {
            // table no longer exists, (table might have been deleted and therefore no longer in use).
            success = true;
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
                task = taskman_create(TASK_ORIGIN_INTERNAL, TASK_WT_DUMP, NULL, NULL);
                if (NULL != task)
                {
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

    default:
        CX_WARN(CX_ALW, "undefined <main-thread> behaviour for task type #%d.", _task->type);
        break;
    }

    return success;
}

static bool task_reschedule(task_t* _task)
{
    table_t* table = NULL;

    if (INVALID_HANDLE != _task->tableHandle)
    {
        table = &g_ctx.tables[_task->tableHandle];

        queue_push(table->blockedQueue, _task);
        _task->state = TASK_STATE_BLOCKED_AWAITING;
    }

    return true;
}

static bool task_completed(task_t* _task)
{
    table_t* table = NULL;
   
    if (INVALID_HANDLE != _task->tableHandle)
        table = &g_ctx.tables[_task->tableHandle];

    switch (_task->type)
    {
    case TASK_WT_CREATE:
    {
        if (table->deleted)
        {
            // the creation failed, we need to free this table now.
            table_free(table);
        }
        else if (ERR_NONE == _task->err.code)
        {
            // table creation succeeded, let's create the compaction timer so that we get notified when we need to compact it.
            table->timerHandle = cx_timer_add(table->meta.compactionInterval, LFS_TIMER_COMPACT, table);
            CX_CHECK(INVALID_HANDLE != table->timerHandle, "we ran out of timer handles for table '%s'!", table->meta.name);

            // unblock the table making it fully available!
            table_unblock(table);
        }

        if (TASK_ORIGIN_API == _task->origin)
            api_response_create(_task);
        else
            cli_report_create(_task);
        break;
    }

    case TASK_WT_DROP:
    {
        if (ERR_NONE == _task->err.code)
        {
            // drop succeeded, free this table in a thread-safe way (this is the main-thread).
            table_free(table);
        }

        if (TASK_ORIGIN_API == _task->origin)
            api_response_drop(_task);
        else
            cli_report_drop(_task);
        break;
    }

    case TASK_WT_DESCRIBE:
    {
        if (TASK_ORIGIN_API == _task->origin)
            api_response_describe(_task);
        else
            cli_report_describe(_task);
        break;
    }

    case TASK_WT_SELECT:
    {
        if (TASK_ORIGIN_API == _task->origin)
            api_response_select(_task);
        else
            cli_report_select(_task);
        break;
    }

    case TASK_WT_INSERT:
    {
        if (TASK_ORIGIN_API == _task->origin)
            api_response_insert(_task);
        else
            cli_report_insert(_task);
        break;
    }

    case TASK_WT_DUMP:
    {
        cli_report_dumped(_task, table->meta.name, "asd.tmp"); //TODO add resulting dump file name
        break;
    }

    case TASK_WT_COMPACT:
    {
        double blockedTime = 0;
        if (NULL != table)
        {
            if (table->inUse)
            {
                table->compacting = false;
                table_unblock(table);
            }
            blockedTime = cx_time_counter() - table->blockedStartTime;
        }
        else
        {
            blockedTime = 0;
        }
        cli_report_compact(_task, blockedTime);
        break;
    }

    case TASK_MT_DUMP:
    {
        //noop
        break;
    }

    case TASK_MT_COMPACT:
    {
        //noop
        break;
    }

    default:
        CX_WARN(CX_ALW, "undefined <completed> behaviour for request type #%d.", _task->type);
        break;
    }

    if (TASK_ORIGIN_CLI == _task->origin)
    {
        // mark the current (processed) command as done
        cx_cli_command_end();
    }

    return true;
}

static bool task_free(task_t* _task)
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
        //noop
        break;
    }

    case TASK_WT_COMPACT:
    {
        data_compact_t* data = (data_compact_t*)_task->data;
        //noop
        break;
    }

    case TASK_MT_COMPACT:
    {
        //noop
        break;
    }

    case TASK_MT_DUMP:
    {
        //noop
        break;
    }

    default:
        CX_WARN(CX_ALW, "undefined <free> behaviour for request type #%d.", _task->type);
        break;
    }

    return true;
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