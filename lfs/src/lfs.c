#include "lfs.h"
#include "memtable.h"
#include "lfs_worker.h"
#include "fs.h"

#include <ker/cli_parser.h>
#include <ker/reporter.h>
#include <ker/taskman.h>
#include <ker/common.h>

#include <cx/cx.h>
#include <cx/timer.h>
#include <cx/mem.h>
#include <cx/file.h>
#include <cx/str.h>
#include <cx/cli.h>
#include <cx/file.h>
#include <cx/cdict.h>
#include <cx/sort.h>
#include <cx/math.h>
#include <cx/fswatch.h>

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

static bool         cfg_load(const char* _cfgFilePath, cx_err_t* _err);

static bool         lfs_init(cx_err_t* _err);
static void         lfs_destroy();

static bool         net_init(cx_err_t* _err);
static void         net_destroy();

static void         handle_cli_command(const cx_cli_cmd_t* _cmd);
static bool         handle_timer_tick(uint64_t _expirations, uint32_t _id, void* _userData);
static void         handle_fswatch_event(const char* _path, uint32_t _mask, void* _userData);

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
        && cx_fswatch_init(1, (cx_fswatch_handler_cb)handle_fswatch_event, &err)
        && cfg_load((_argc > 1) ? _argv[1] : "res/lfs.cfg", &err)
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

            // poll fswatch events
            cx_fswatch_poll_events();

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
    cx_cli_destroy();       // destroys command line interface.
    taskman_stop();         // denies new tasks creation & pauses the pool so that current enqueued tasks can't start executing.
    net_destroy();          // destroys the timers & server context serving MEM nodes.
    taskman_destroy();      // safely destroys the pool and the blocked queues.
    lfs_destroy();          // destroys filesystem.
    cx_timer_destroy();     // destroys timers.
    cx_fswatch_destroy();   // destroys fs watcher.

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

static bool cfg_load(const char* _cfgFilePath, cx_err_t* _err)
{
    char* key = "";
    bool isReloading = false;

    if (!g_ctx.cfgInitialized)
    {
        g_ctx.cfgInitialized = true;
        cx_file_path(&g_ctx.cfgFilePath, "%s", _cfgFilePath);
    }
    else
    {
        isReloading = true;
    }

    t_config* cfg = config_create(g_ctx.cfgFilePath);
    if (NULL != cfg)
    {
        ////////////////////////////////////////////////////////////////////////////////////////
        // NON-RELOADABLE PROPERTIES
        if (!isReloading)
        {
            CX_INFO("config file: %s", g_ctx.cfgFilePath);

            key = LFS_CFG_PASSWORD;
            if (!cfg_get_password(cfg, key, &g_ctx.cfg.password)) goto key_missing;

            key = LFS_CFG_LISTENING_IP;
            if (!cfg_get_string(cfg, key, g_ctx.cfg.listeningIp, sizeof(g_ctx.cfg.listeningIp))) goto key_missing;

            key = LFS_CFG_LISTENING_PORT;
            if (!cfg_get_uint16(cfg, key, &g_ctx.cfg.listeningPort)) goto key_missing;

            key = LFS_CFG_WORKERS;
            if (!cfg_get_uint16(cfg, key, &g_ctx.cfg.workers)) goto key_missing;

            key = LFS_CFG_ROOT_DIR;
            if (!cfg_get_string(cfg, key, g_ctx.cfg.rootDir, sizeof(g_ctx.cfg.rootDir))) goto key_missing;

            key = LFS_CFG_BLOCKS_COUNT;
            if (!cfg_get_uint32(cfg, key, &g_ctx.cfg.blocksCount)) goto key_missing;

            key = LFS_CFG_BLOCKS_SIZE;
            if (!cfg_get_uint32(cfg, key, &g_ctx.cfg.blocksSize)) goto key_missing;

            key = LFS_CFG_VALUE_SIZE;
            if (!cfg_get_uint16(cfg, key, &g_ctx.cfg.valueSize)) goto key_missing;
        }

        ////////////////////////////////////////////////////////////////////////////////////////
        // RELOADABLE PROPERTIES
        key = LFS_CFG_DELAY;
        if (!cfg_get_uint32(cfg, key, &g_ctx.cfg.delay)) goto key_missing;

        key = LFS_CFG_INT_DUMP;
        if (!cfg_get_uint32(cfg, key, &g_ctx.cfg.dumpInterval)) goto key_missing;

        config_destroy(cfg);
        return true;

    key_missing:
        CX_ERR_SET(_err, ERR_CFG_MISSINGKEY, "key '%s' is missing in the configuration file.", key);
    }
    else
    {
        CX_ERR_SET(_err, ERR_CFG_NOTFOUND, "configuration file '%s' is missing or not readable.", g_ctx.cfgFilePath);
    }

    if (NULL != cfg)
        config_destroy(cfg);

    return false;
}

static bool lfs_init(cx_err_t* _err)
{
    g_ctx.timerDump = cx_timer_add(g_ctx.cfg.dumpInterval, LFS_TIMER_DUMP, NULL);
    if (INVALID_HANDLE == g_ctx.timerDump)
    {
        CX_ERR_SET(_err, ERR_INIT_TIMER, "dump timer creation failed.");
        return false;
    }

    g_ctx.cfgFswatchHandle = cx_fswatch_add(g_ctx.cfgFilePath, IN_MODIFY, NULL);
    if (INVALID_HANDLE == g_ctx.cfgFswatchHandle)
    {
        CX_ERR_SET(_err, ERR_INIT_FSWATCH, "cfg fswatch creation failed.");
        return false;
    }

    return fs_init(g_ctx.cfg.rootDir, g_ctx.cfg.blocksCount, g_ctx.cfg.blocksSize, _err);
}

static void lfs_destroy()
{
    fs_destroy();

    if (INVALID_HANDLE != g_ctx.timerDump)
    {
        cx_timer_remove(g_ctx.timerDump);
        g_ctx.timerDump = INVALID_HANDLE;
    }

    if (INVALID_HANDLE != g_ctx.cfgFswatchHandle)
    {
        cx_fswatch_remove(g_ctx.cfgFswatchHandle);
        g_ctx.cfgFswatchHandle = INVALID_HANDLE;
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

    QUERY_TYPE  query = common_parse_query(_cmd->header);
    char*       tableName = NULL;
    uint16_t    key = 0;
    char*       value = NULL;
    uint64_t    timestamp = 0;
    uint8_t     consistency = 0;
    uint16_t    numPartitions = 0;
    uint32_t    compactionInterval = 0;
    uint32_t    packetSize = 0;

    if (QUERY_EXIT == query)
    {
        g_ctx.isRunning = false;
        g_ctx.shutdownReason = "cli-issued exit";
    }
    else if (QUERY_LOGFILE == query)
    {
        if (NULL != cx_logfile())
        {
            report_info(cx_logfile(), stdout);
        }
        else
        {
            report_info("There is no log file available.", stdout);
        }
        cx_cli_command_end();
    }
    else if (QUERY_CREATE == query)
    {
        if (cli_parse_create(_cmd, &err, &tableName, &consistency, &numPartitions, &compactionInterval))
        {
            packetSize = lfs_pack_req_create(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, consistency, numPartitions, compactionInterval);
            lfs_handle_req_create((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (QUERY_DROP == query)
    {
        if (cli_parse_drop(_cmd, &err, &tableName))
        {
            packetSize = lfs_pack_req_drop(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName);
            lfs_handle_req_drop((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (QUERY_DESCRIBE == query)
    {
        if (cli_parse_describe(_cmd, &err, &tableName))
        {
            packetSize = lfs_pack_req_describe(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName);
            lfs_handle_req_describe((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (QUERY_SELECT == query)
    {
        if (cli_parse_select(_cmd, &err, &tableName, &key))
        {
            packetSize = lfs_pack_req_select(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, key);
            lfs_handle_req_select((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (QUERY_INSERT == query)
    {
        if (cli_parse_insert(_cmd, &err, &tableName, &key, &value, &timestamp))
        {
            packetSize = lfs_pack_req_insert(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, key, value, timestamp);
            lfs_handle_req_insert((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else
    {
        CX_ERR_SET(&err, 1, "Unknown command '%s'.", _cmd->header);
    }

    if (0 != err.code)
    {
        report_error(&err, stdout);
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

static void handle_fswatch_event(const char* _path, uint32_t _mask, void* _userData)
{
    cx_err_t err;
    CX_ERR_CLEAR(&err);

    if (cfg_load(NULL, &err))
    {
        cx_timer_modify(g_ctx.timerDump, g_ctx.cfg.dumpInterval);
        CX_INFO("configuration file successfully reloaded.");
    }
    else
    {
        CX_INFO("configuration file reload failed. %s", err.desc);
    }
}

static bool on_connection_mem(cx_net_ctx_sv_t* _ctx, const ipv4_t _ipv4)
{
    return g_ctx.isRunning;
}

static void on_disconnection_mem(cx_net_ctx_sv_t* _ctx, cx_net_client_t* _client)
{
    // MEM node just disconnected.
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
        success = fs_table_compact_tryenqueue(data->tableName);
        break;
    }

    case TASK_MT_DUMP:
    {
        success = fs_table_dump_tryenqueue();
        break;
    }

    case TASK_MT_FREE:
    {
        data_free_t* data = _task->data;

        if (RESOURCE_TYPE_TABLE == data->resourceType)
        {
            table = (table_t*)data->resourcePtr;

            if (0 == cx_reslock_counter(&table->reslock) && !table->compacting)
            {
                // at this point the table no longer exist (it's not part of the tablesMap dictionary)
                // and also it's not during a compaction (important!)
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
            fs_table_unblock(table);
        }
        else
        {
            // the creation failed, we need to free this table slot now.
            fs_table_free(table);
        }

        if (TASK_ORIGIN_API == _task->origin)
            api_response_create(_task);
        else
            report_create(_task, stdout);
        break;
    }

    case TASK_WT_DROP:
    {
        if (ERR_NONE == _task->err.code)
        {
            // drop succeeded, free this table in a thread-safe way (this is the main-thread).
            fs_table_free(table);
        }

        if (TASK_ORIGIN_API == _task->origin)
            api_response_drop(_task);
        else
            report_drop(_task, stdout);
        break;
    }

    case TASK_WT_DESCRIBE:
    {
        if (TASK_ORIGIN_API == _task->origin)
            api_response_describe(_task);
        else
            report_describe(_task, stdout);
        break;
    }

    case TASK_WT_SELECT:
    {
        if (TASK_ORIGIN_API == _task->origin)
            api_response_select(_task);
        else
            report_select(_task, stdout);
        break;
    }

    case TASK_WT_INSERT:
    {
        if (TASK_ORIGIN_API == _task->origin)
            api_response_insert(_task);
        else
            report_insert(_task, stdout);
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
            else if (ERR_DUMP_NOT_NEEDED != _task->err.code)
            {
                CX_INFO("table '%s' dump failed. %s", table->meta.name, _task->err.desc);
            }
        }
        break;
    }

    case TASK_WT_COMPACT:
    {      
        CX_CHECK_NOT_NULL(table);
        if (NULL != table)
        {
            table->compacting = false;

            data_compact_t* data = _task->data;

            if (ERR_NONE == _task->err.code && data->dumpsCount > 0)
            {
                CX_INFO("table '%s' compacted %d files successfully in %.3f seconds (%.3f sec blocked)", 
                    table->meta.name, data->dumpsCount, cx_time_counter() - _task->startTime,
                    data->beginStageTime + data->endStageTime);
            }
            else if (ERR_NONE != _task->err.code)
            {
                CX_INFO("table '%s' compaction failed. %s", table->meta.name, _task->err.desc);
            }
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
    return common_task_data_free(_task->type, _task->data);
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
    uint32_t pos = 0;
    uint16_t tablesPacked = 0;

    while (!common_pack_res_describe(g_ctx.buff1, sizeof(g_ctx.buff1), &pos,
        _task->remoteId, data->tables, data->tablesCount, &tablesPacked, &_task->err))
    {
        cx_net_send(g_ctx.sv, MEMP_RES_DESCRIBE, g_ctx.buff1, pos, _task->clientHandle);
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