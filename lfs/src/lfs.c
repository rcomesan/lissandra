#include "lfs.h"
#include "memtable.h"
#include "lfs_worker.h"
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
#include <cx/binw.h> //TODO sacar esto una vez que mueva api_response_describe a common
#include <cx/math.h>

#include <ker/common_protocol.h>
#include <lfs/lfs_protocol.h>
#include <mem/mem_protocol.h>
#include <commons/config.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef DEBUG
#define OUTPUT_LOG_ENABLED false
#else
#define OUTPUT_LOG_ENABLED true
#endif 

lfs_ctx_t           g_ctx;                                  // global LFS context

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool         cfg_init(const char* _cfgFilePath, cx_err_t* _err);
static void         cfg_destroy();

static bool         lfs_init(cx_err_t* _err);
static void         lfs_destroy();

static bool         net_init(cx_err_t* _err);
static void         net_destroy();

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

static bool         on_connection_mem(cx_net_ctx_sv_t* _ctx, const ipv4_t _ipv4);
static void         on_disconnection_mem(cx_net_ctx_sv_t* _ctx, cx_net_client_t* _client);

static void         table_unblock(table_t* _table);
static void         table_free(table_t* _table);
static void         table_create_task_dump(const char* _tableName, table_t* _table, void* _userData);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

int main(int _argc, char** _argv)
{
    cx_err_t err;

    CX_MEM_ZERO(g_ctx);
    CX_ERR_CLEAR(&err);

    double timeCounter = 0;
    double timeCounterPrev = 0;
    double timeDelta = 0;

    g_ctx.shutdownReason = "main thread finished";
    g_ctx.isRunning = true
        && cx_init(PROJECT_NAME, OUTPUT_LOG_ENABLED, NULL, &err)
        && cx_timer_init(MAX_TABLES + LFS_TIMER_COUNT, handle_timer_tick, &err)
        && cfg_init("res/lfs.cfg", &err)
        && taskman_init(g_ctx.cfg.workers, task_run_mt, task_run_wk, task_completed, task_free, task_reschedule, &err)
        && lfs_init(&err)
        && net_init(&err)
        && cx_cli_init(&err);

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
            cx_net_poll_events(g_ctx.sv, 0);
            
            // poll timer events
            cx_timer_poll_events();

            // update tasks
            taskman_update();

            // pass control back to the os scheduler
            sleep(0);
        }
    }
    else 
    {
        g_ctx.shutdownReason = "initialization failed";
        CX_WARN(CX_ALW, "initialization failed (errcode %d). %s", err.code, err.desc);
    }

    CX_INFO("node is shutting down. reason: %s.", g_ctx.shutdownReason);
    cx_cli_destroy();
    taskman_destroy();
    net_destroy();
    lfs_destroy();
    cfg_destroy();
    cx_timer_destroy();

    if (ERR_NONE == err.code)
    {
        CX_INFO("node terminated gracefully.");
    }
    else
    {
        CX_INFO("node terminated with error %d.", err.code);
    }
    
    cx_destroy();
    return err.code;
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

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

        key = "password";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            temp = config_get_string_value(g_ctx.cfg.handle, key);

            uint32_t len = strlen(temp);
            CX_WARN(len >= MIN_PASSWD_LEN, "'%s' must have a minimum length of %d characters!", key, MIN_PASSWD_LEN);
            CX_WARN(len <= MAX_PASSWD_LEN, "'%s' must have a maximum length of %s characters!", key, MAX_PASSWD_LEN)
            if (!cx_math_in_range(len, MIN_PASSWD_LEN, MAX_PASSWD_LEN)) goto key_missing;

            cx_str_copy(g_ctx.cfg.password, sizeof(g_ctx.cfg.password), temp);
        }
        else
        {
            goto key_missing;
        }

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
         CX_ERR_SET(_err, ERR_CFG_MISSINGKEY, "key '%s' is missing in the configuration file.", key);
    }
    else
    {
        CX_ERR_SET(_err, ERR_CFG_NOTFOUND, "configuration file '%s' is missing or not readable.", cfgPath);
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
    g_ctx.timerDump = cx_timer_add(g_ctx.cfg.dumpInterval, LFS_TIMER_DUMP, NULL);
    if (INVALID_HANDLE == g_ctx.timerDump)
    {
        CX_ERR_SET(_err, ERR_INIT_TIMER, "thread pool creation failed.");
        return false;
    }

    return fs_init(_err);
}

static void lfs_destroy()
{
    fs_destroy();

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
    svCtxArgs.maxClients = 100;
    svCtxArgs.onConnection = (cx_net_on_connection_cb)on_connection_mem;
    svCtxArgs.onDisconnection = (cx_net_on_disconnection_cb)on_disconnection_mem;

    // message headers to handlers mappings
    svCtxArgs.msgHandlers[LFSP_AUTH] = (cx_net_handler_cb)lfs_handle_auth;
    svCtxArgs.msgHandlers[LFSP_REQ_CREATE] = (cx_net_handler_cb)lfs_handle_req_create;
    svCtxArgs.msgHandlers[LFSP_REQ_DROP] = (cx_net_handler_cb)lfs_handle_req_drop;
    svCtxArgs.msgHandlers[LFSP_REQ_DESCRIBE] = (cx_net_handler_cb)lfs_handle_req_describe;
    svCtxArgs.msgHandlers[LFSP_REQ_SELECT] = (cx_net_handler_cb)lfs_handle_req_select;
    svCtxArgs.msgHandlers[LFSP_REQ_INSERT] = (cx_net_handler_cb)lfs_handle_req_insert;

    // start server context and start listening for requests
    g_ctx.sv = cx_net_listen(&svCtxArgs);

    if (NULL != g_ctx.sv)
    {
        return true;
    }
    else
    {
        CX_ERR_SET(_err, ERR_INIT_NET, "could not start a listening server context on %s:%d.",
            svCtxArgs.ip, svCtxArgs.port);
        return false;
    }
}

static void net_destroy()
{
    cx_net_destroy(g_ctx.sv);
    g_ctx.sv = NULL;
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
        g_ctx.shutdownReason = "cli-issued exit";
    }
    else if (strcmp("LOGFILE", _cmd->header) == 0)
    {
        if (NULL != cx_logfile())
        {
            cli_report_info(cx_logfile());
        }
        else
        {
            cli_report_info("There is no log file available.");
        }
        cx_cli_command_end();
    }
    else if (strcmp("CREATE", _cmd->header) == 0)
    {
        if (cli_parse_create(_cmd, &err, &tableName, &consistency, &numPartitions, &compactionInterval))
        {
            packetSize = lfs_pack_req_create(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, consistency, numPartitions, compactionInterval);
            lfs_handle_req_create((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("DROP", _cmd->header) == 0)
    {
        if (cli_parse_drop(_cmd, &err, &tableName))
        {
            packetSize = lfs_pack_req_drop(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName);
            lfs_handle_req_drop((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("DESCRIBE", _cmd->header) == 0)
    {
        if (cli_parse_describe(_cmd, &err, &tableName))
        {
            packetSize = lfs_pack_req_describe(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName);
            lfs_handle_req_describe((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("SELECT", _cmd->header) == 0)
    {
        if (cli_parse_select(_cmd, &err, &tableName, &key))
        {
            packetSize = lfs_pack_req_select(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, key);
            lfs_handle_req_select((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("INSERT", _cmd->header) == 0)
    {
        if (cli_parse_insert(_cmd, &err, &tableName, &key, &value, &timestamp))
        {
            packetSize = lfs_pack_req_insert(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, key, value, timestamp);
            lfs_handle_req_insert((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
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
            data_compact_t* data = CX_MEM_STRUCT_ALLOC(data);
            cx_str_copy(data->tableName, sizeof(data->tableName), ((table_t*)_userData)->meta.name);

            task->data = data;
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

static bool on_connection_mem(cx_net_ctx_sv_t* _ctx, const ipv4_t _ipv4)
{
    return g_ctx.isRunning;
}

static void on_disconnection_mem(cx_net_ctx_sv_t* _ctx, cx_net_client_t* _client)
{
    // MEM node just disconnected.
}

static void table_unblock(table_t* _table)
{
    // make the resource (our table) available again
    cx_reslock_unblock(&_table->reslock);
    
    task_t* task = NULL;
    while (!queue_is_empty(_table->blockedQueue))
    {
        task = queue_pop(_table->blockedQueue);
        task->state = TASK_STATE_NEW;
    }
}

static void table_free(table_t* _table)
{
    data_free_t* data = CX_MEM_STRUCT_ALLOC(data);
    data->resourceType = RESOURCE_TYPE_TABLE;
    data->resourcePtr = _table;

    task_t* task = taskman_create(TASK_ORIGIN_INTERNAL, TASK_MT_FREE, data, NULL);
    if (NULL != task)
    {
        task->state = TASK_STATE_NEW;
    }
}

static void table_create_task_dump(const char* _tableName, table_t* _table, void* _userData)
{
    task_t* task = taskman_create(TASK_ORIGIN_INTERNAL, TASK_WT_DUMP, NULL, NULL);
    if (NULL != task)
    {
        data_dump_t* data = CX_MEM_STRUCT_ALLOC(data);
        cx_str_copy(data->tableName, sizeof(data->tableName), _table->meta.name);
        
        task->data = data;
        task->state = TASK_STATE_NEW;
    }
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
        data_compact_t* data = _task->data;
        
        if (fs_table_avail_guard_begin(data->tableName, NULL, &table))
        {
            if (!table->compacting)
            {
                if (!cx_reslock_is_blocked(&table->reslock))
                {
                    // if the table is not blocked, block it now to start denying tasks
                    cx_reslock_block(&table->reslock);
                }

                if (1 == cx_reslock_counter(&table->reslock))
                {
                    // at this point, our table is blocked (so new operations on it are being blocked and delayed)
                    // and also there's just only 1 pending operation on it (that's us) we can safely start compacting it now.
                    table->compacting = true;

                    task = taskman_create(TASK_ORIGIN_INTERNAL, TASK_WT_COMPACT, NULL, NULL);
                    if (NULL != task)
                    {
                        data_compact_t* data = CX_MEM_STRUCT_ALLOC(data);
                        cx_str_copy(data->tableName, sizeof(data->tableName), table->meta.name);

                        task->data = data;
                        task->table = table;
                        task->state = TASK_STATE_NEW;
                        success = true;
                    }
                    else
                    {
                        // task creation failed. unblock this table and skip this task.
                        table->compacting = false;
                        table_unblock(table);
                    }
                }
            }
            else
            {
                // we can safely ignore this one
                // we don't really want multiple compactions to be performed at the same time
                CX_INFO("Ignoring compaction for table '%s' (another thread is already compacting it).", table->meta.name);
                success = true;
            }

            fs_table_avail_guard_end(table);
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
        fs_tables_foreach((fs_func_cb)table_create_task_dump, NULL);        

        success = true;
        break;
    }

    case TASK_MT_FREE:
    {
        data_free_t* data = _task->data;

        if (RESOURCE_TYPE_TABLE == data->resourceType)
        {
            table = (table_t*)data->resourcePtr;

            if (0 == cx_reslock_counter(&table->reslock))
            {
                // at this point the table no longer exist (it's not part of the tablesMap dictionary)
                // remove and re-schedule all the tasks in the blocked queue
                // so that they finally complete with 'table does not exist' error.
                task_t* task = NULL;
                while (!queue_is_empty(table->blockedQueue))
                {
                    task = queue_pop(table->blockedQueue);
                    task->state = TASK_STATE_NEW;
                }

                // destroy & deallocate table
                fs_table_destroy(table);
                success = true;
            }
        }
        else
        {
            CX_WARN(CX_ALW, "undefined TASK_MT_FREE for resource type #%d!", data->resourceType);
            success = true;
        }
        break;
    }

    default:
        CX_WARN(CX_ALW, "undefined <main-thread> behaviour for task type #%d.", _task->type);
        success = true;
        break;
    }

    return success;
}

static bool task_reschedule(task_t* _task)
{
    if (NULL != _task->table)
    {
        queue_push(((table_t*)_task->table)->blockedQueue, _task);
        _task->state = TASK_STATE_BLOCKED_AWAITING;
    }

    return true;
}

static bool task_completed(task_t* _task)
{
    table_t* table = _task->table;

    switch (_task->type)
    {
    case TASK_WT_CREATE:
    {
        if (ERR_NONE == _task->err.code)
        {
            // table creation succeeded, let's create the compaction timer so that we get notified when we need to compact it.
            table->timerHandle = cx_timer_add(table->meta.compactionInterval, LFS_TIMER_COMPACT, table);
            CX_CHECK(INVALID_HANDLE != table->timerHandle, "we ran out of timer handles for table '%s'!", table->meta.name);

            // unblock the table making it fully available!
            table_unblock(table);
        }
        else
        {
            // the creation failed, we need to free this table slot now.
            table_free(table);
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
        if (NULL != table)
        {
            if (ERR_NONE == _task->err.code)
            {
                CX_INFO("table '%s' dumped successfully.", table->meta.name);
            }
            else
            {
                CX_INFO("table '%s' dump failed. %s", table->meta.name, _task->err.desc);
            }
        }
        break;
    }

    case TASK_WT_COMPACT:
    {
        double blockedTime = 0;
        if (NULL != table)
        {
            table->compacting = false;
            table_unblock(table);
            blockedTime = cx_reslock_blocked_time(&table->reslock);
        }
        else
        {
            blockedTime = 0;
        }

        if (ERR_NONE == _task->err.code)
        {
            CX_INFO("table '%s' compacted successfully. (%.3f sec blocked)", table->meta.name, blockedTime);
        }
        else
        {
            CX_INFO("table '%s' compaction failed. %s", table->meta.name, _task->err.desc);
        }
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

    case TASK_MT_FREE:
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

    case TASK_MT_FREE:
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
    uint32_t payloadSize = mem_pack_res_create(g_ctx.buff1, sizeof(g_ctx.buff1), 
        _task->remoteId, &_task->err);    

    cx_net_send(g_ctx.sv, MEMP_RES_CREATE, g_ctx.buff1, payloadSize, _task->clientHandle);
}

static void api_response_drop(const task_t* _task)
{
    uint32_t payloadSize = mem_pack_res_drop(g_ctx.buff1, sizeof(g_ctx.buff1),
        _task->remoteId, &_task->err);

    cx_net_send(g_ctx.sv, MEMP_RES_DROP, g_ctx.buff1, payloadSize, _task->clientHandle);
}

static void api_response_describe(const task_t* _task)
{
    data_describe_t* data = _task->data;

    uint32_t tableSize = 0;
    uint32_t pos = 0;
    cx_binw_uint16(g_ctx.buff1, sizeof(g_ctx.buff1), &pos, _task->remoteId);
    cx_binw_uint16(g_ctx.buff1, sizeof(g_ctx.buff1), &pos, data->tablesCount);

    if (1 == data->tablesCount)
    {
        cx_binw_uint32(g_ctx.buff1, sizeof(g_ctx.buff1), &pos, _task->err.code);
        if (ERR_NONE != _task->err.code)
        {
            cx_binw_str(g_ctx.buff1, sizeof(g_ctx.buff1), &pos, _task->err.desc);
            cx_net_send(g_ctx.sv, MEMP_RES_DESCRIBE, g_ctx.buff1, pos, _task->clientHandle);
            return;
        }
    }

    // start sending them in chunks
    for (uint16_t i = 0; i < data->tablesCount; i++)
    {
        // pack this table's metadata into temp buffer #2
        tableSize = common_pack_table_meta(g_ctx.buff2, sizeof(g_ctx.buff2), &data->tables[i]);
        
        if (tableSize > sizeof(g_ctx.buff1) - pos)
        {
            // not enough space to append this one, flush our packet
            cx_net_send(g_ctx.sv, MEMP_RES_DESCRIBE, g_ctx.buff1, pos, _task->clientHandle);

            // start a new packet
            pos = 0;
            cx_binw_uint16(g_ctx.buff1, sizeof(g_ctx.buff1), &pos, _task->remoteId);
        }
        
        // append it
        memcpy(&g_ctx.buff1[pos], g_ctx.buff2, tableSize);
        pos += tableSize;
    }
    
    if (pos > sizeof(uint16_t))
    {
        cx_net_send(g_ctx.sv, MEMP_RES_DESCRIBE, g_ctx.buff1, pos, _task->clientHandle);
    }
}

static void api_response_select(const task_t* _task)
{
    data_select_t* data = _task->data;
    uint32_t payloadSize = mem_pack_res_select(g_ctx.buff1, sizeof(g_ctx.buff1),
        _task->remoteId, &_task->err, &data->record);

    cx_net_send(g_ctx.sv, MEMP_RES_SELECT, g_ctx.buff1, payloadSize, _task->clientHandle);
}

static void api_response_insert(const task_t* _task)
{
    uint32_t payloadSize = mem_pack_res_insert(g_ctx.buff1, sizeof(g_ctx.buff1),
        _task->remoteId, &_task->err);
    
    cx_net_send(g_ctx.sv, MEMP_RES_INSERT, g_ctx.buff1, payloadSize, _task->clientHandle);
}