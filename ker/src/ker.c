#include "ker.h"
#include "ker_worker.h"
#include "mempool.h"

#include <ker/cli_parser.h>
#include <ker/reporter.h>
#include <ker/taskman.h>
#include <ker/common.h>
#include <ker/gossip.h>
#include <ker/ker_protocol.h>
#include <mem/mem_protocol.h>

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/file.h>
#include <cx/net.h>
#include <cx/str.h>
#include <cx/math.h>
#include <cx/timer.h>
#include <cx/fswatch.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef DEBUG
#define OUTPUT_LOG_ENABLED false
#else
#define OUTPUT_LOG_ENABLED true
#endif 

ker_ctx_t           g_ctx;

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool         cfg_load(const char* _cfgFilePath, cx_err_t* _err);

static bool         ker_init(cx_err_t* _err);
static void         ker_destroy();

static void         handle_cli_command(const cx_cli_cmd_t* _cmd);
static bool         handle_timer_tick(uint64_t _expirations, uint32_t _id, void* _userData);
static void         handle_fswatch_event(const char* _path, uint32_t _mask, void* _userData);

static bool         task_run_mt(task_t* _task);
static bool         task_run_wk(task_t* _task);
static bool         task_completed(task_t* _task);
static bool         task_free(task_t* _task);
static bool         task_reschedule(task_t* _task);

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
        && cx_timer_init(KER_TIMER_COUNT, handle_timer_tick, &err)
        && cx_fswatch_init(1, (cx_fswatch_handler_cb)handle_fswatch_event, &err)
        && cfg_load((_argc > 1) ? _argv[1] : "res/ker.cfg", &err)
        && taskman_init(g_ctx.cfg.workers, task_run_mt, task_run_wk, task_completed, task_free, task_reschedule, &err)
        && ker_init(&err)
        && cx_cli_init(&err);

    if (ERR_NONE == err.code)
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
            mempool_poll_events();

            // poll timer events
            cx_timer_poll_events();

            // poll fswatch events
            cx_fswatch_poll_events();

            // poll gossip events
            gossip_poll_events();

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
    mempool_disconnect();   // destroys all the communication contexts. wakes up & aborts all the tasks waiting on remote responses.
    taskman_destroy();      // safely destroys the pool and the blocked queues.
    ker_destroy();          // destroys mempool and timers.
    cx_timer_destroy();     // destroys timers.
    cx_fswatch_destroy();   // destroys fs watcher.

    if (0 == err.code)
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

            key = KER_CFG_WORKERS;
            if (!cfg_get_uint16(cfg, key, &g_ctx.cfg.workers)) goto key_missing;

            key = KER_CFG_MEM_IP;
            if (!cfg_get_string(cfg, key, g_ctx.cfg.seed.ip, sizeof(g_ctx.cfg.seed.ip))) goto key_missing;

            key = KER_CFG_MEM_PORT;
            if (!cfg_get_uint16(cfg, key, &g_ctx.cfg.seed.port)) goto key_missing;

            key = KER_CFG_MEM_PASSWORD;
            if (!cfg_get_password(cfg, key, &g_ctx.cfg.memPassword)) goto key_missing;
        }

        ////////////////////////////////////////////////////////////////////////////////////////
        // RELOADABLE PROPERTIES
        key = KER_CFG_DELAY_RUN;
        if (!cfg_get_uint32(cfg, key, &g_ctx.cfg.delayRun)) goto key_missing;

        key = KER_CFG_QUANTUM;
        if (!cfg_get_uint8(cfg, key, &g_ctx.cfg.quantum)) goto key_missing;

        key = KER_CFG_INT_METAREFRESH;
        if (!cfg_get_uint32(cfg, key, &g_ctx.cfg.intervalMetaRefresh)) goto key_missing;

        key = KER_CFG_INT_GOSSIPING;
        if (!cfg_get_uint32(cfg, key, &g_ctx.cfg.intervalGossiping)) goto key_missing;

        config_destroy(cfg);
        return true;

    key_missing:
        CX_ERR_SET(_err, ERR_CFG_MISSINGKEY, "key '%s' is either missing or invalid in the configuration file.", key);
    }
    else
    {
        CX_ERR_SET(_err, ERR_CFG_NOTFOUND, "configuration file '%s' is missing or not readable.", g_ctx.cfgFilePath);
    }

    if (NULL != cfg)
        config_destroy(cfg);

    return false;
}

static bool ker_init(cx_err_t* _err)
{
    g_ctx.timerMetaRefresh = cx_timer_add(g_ctx.cfg.intervalMetaRefresh, KER_TIMER_METAREFRESH, NULL);
    if (INVALID_HANDLE == g_ctx.timerMetaRefresh)
    {
        CX_ERR_SET(_err, ERR_INIT_TIMER, "metarefresh timer creation failed.");
        return false;
    }

    g_ctx.timerGossip = cx_timer_add(g_ctx.cfg.intervalGossiping, KER_TIMER_GOSSIP, NULL);
    if (INVALID_HANDLE == g_ctx.timerGossip)
    {
        CX_ERR_SET(_err, ERR_INIT_TIMER, "gossip timer creation failed.");
        return false;
    }

    g_ctx.cfgFswatchHandle = cx_fswatch_add(g_ctx.cfgFilePath, IN_MODIFY, NULL);
    if (INVALID_HANDLE == g_ctx.cfgFswatchHandle)
    {
        CX_ERR_SET(_err, ERR_INIT_FSWATCH, "cfg fswatch creation failed.");
        return false;
    }

    return true
        && mempool_init(_err)
        && gossip_init(&g_ctx.cfg.seed, 1, _err);
}

static void ker_destroy()
{
    if (INVALID_HANDLE != g_ctx.timerMetaRefresh)
    {
        cx_timer_remove(g_ctx.timerMetaRefresh);
        g_ctx.timerMetaRefresh = INVALID_HANDLE;
    }

    if (INVALID_HANDLE != g_ctx.timerGossip)
    {
        cx_timer_remove(g_ctx.timerGossip);
        g_ctx.timerGossip = INVALID_HANDLE;
    }

    if (INVALID_HANDLE != g_ctx.cfgFswatchHandle)
    {
        cx_fswatch_remove(g_ctx.cfgFswatchHandle);
        g_ctx.cfgFswatchHandle = INVALID_HANDLE;
    }

    gossip_destroy();
    mempool_destroy();
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
    cx_path_t   lqlScriptPath;
    cx_path_t   logPath;
    uint32_t    packetSize = 0;
    uint16_t    memNumber = 0;

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
            packetSize = ker_pack_req_create(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, consistency, numPartitions, compactionInterval);
            ker_handle_req_create(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (QUERY_DROP == query)
    {
        if (cli_parse_drop(_cmd, &err, &tableName))
        {
            packetSize = ker_pack_req_drop(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName);
            ker_handle_req_drop(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (QUERY_DESCRIBE == query)
    {
        if (cli_parse_describe(_cmd, &err, &tableName))
        {
            packetSize = ker_pack_req_describe(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName);
            ker_handle_req_describe(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (QUERY_SELECT == query)
    {
        if (cli_parse_select(_cmd, &err, &tableName, &key))
        {
            packetSize = ker_pack_req_select(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, key);
            ker_handle_req_select(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (QUERY_INSERT == query)
    {
        if (cli_parse_insert(_cmd, &err, &tableName, &key, &value, &timestamp))
        {
            packetSize = ker_pack_req_insert(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, key, value, timestamp);
            ker_handle_req_insert(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (QUERY_JOURNAL == query)
    {
        packetSize = ker_pack_req_journal(g_ctx.buff1, sizeof(g_ctx.buff1), 0);
        ker_handle_req_journal(NULL, NULL, g_ctx.buff1, packetSize);
    }
    else if (QUERY_ADDMEMORY == query)
    {
        if (cli_parse_add_memory(_cmd, &err, &memNumber, &consistency))
        {
            packetSize = ker_pack_req_addmem(g_ctx.buff1, sizeof(g_ctx.buff1), 0, memNumber, consistency);
            ker_handle_req_addmem(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (QUERY_RUN == query)
    {
        if (cli_parse_run(_cmd, &err, &lqlScriptPath))
        {
            cx_file_path(&logPath, "%s.log", lqlScriptPath);
            
            packetSize = ker_pack_req_run(g_ctx.buff1, sizeof(g_ctx.buff1), 0, lqlScriptPath, logPath);
            ker_handle_req_run(NULL, NULL, g_ctx.buff1, packetSize);

            report_run(&lqlScriptPath, &logPath, stdout);
            cx_cli_command_end();
        }
    }
    else if (QUERY_MEMPOOL == query)
    {
        mempool_print();
        cx_cli_command_end();
    }
    else
    {
        CX_ERR_SET(&err, 1, "Unknown command '%s'.", _cmd->header);
    }

    if (ERR_NONE != err.code)
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
    case KER_TIMER_METAREFRESH:
    {
        cx_err_t err;

        task_t* task = taskman_create(TASK_ORIGIN_INTERNAL_PRIORITY, TASK_WT_DESCRIBE, NULL, NULL);
        if (NULL != task)
        {
            data_describe_t* data = CX_MEM_STRUCT_ALLOC(data);
            task->data = data;
            task->state = TASK_STATE_NEW;
        }
        CX_WARN(NULL != task, "metadata refresh timer failed (taskman_create returned null)");
        break;
    }

    case KER_TIMER_GOSSIP:
    {
        gossip_run();
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
        cx_timer_modify(g_ctx.timerMetaRefresh, g_ctx.cfg.intervalMetaRefresh);
        cx_timer_modify(g_ctx.timerGossip, g_ctx.cfg.intervalGossiping);
        CX_INFO("configuration file successfully reloaded.");
    }
    else
    {
        CX_INFO("configuration file reload failed. %s", err.desc);
    }        
}

static bool task_run_wk(task_t* _task)
{
    _task->state = TASK_STATE_RUNNING;

    switch (_task->type)
    {
    case TASK_WT_CREATE:
        worker_handle_create(_task, false);
        break;

    case TASK_WT_DROP:
        worker_handle_drop(_task, false);
        break;

    case TASK_WT_DESCRIBE:
        worker_handle_describe(_task, false);
        break;

    case TASK_WT_SELECT:
        worker_handle_select(_task, false);
        break;

    case TASK_WT_INSERT:
        worker_handle_insert(_task, false);
        break;

    case TASK_WT_JOURNAL:
        worker_handle_journal(_task, false);
        break;

    case TASK_WT_ADDMEM:
        worker_handle_addmem(_task, false);
        break;

    case TASK_WT_RUN:
        worker_handle_run(_task);
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

    bool        success = false;
    task_t*     task = NULL;

    switch (_task->type)
    {
    case TASK_MT_FREE:
    {
        data_free_t* data = _task->data;

        if (RESOURCE_TYPE_TABLE == data->resourceType)
        {
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
    if (ERR_QUANTUM_EXHAUSTED == _task->err.code)
    {
        // re-enqueue this task so that it gets executed in a round-robin fashion according to our quantum.
        data_run_t* data = (data_run_t*)_task->data;

        CX_INFO("[%s] quantum of %d exceeded.", data->scriptFileName, g_ctx.cfg.quantum);
        _task->state = TASK_STATE_NEW;
    }
    else
    {
        CX_WARN(CX_ALW, "undefined <reschedule> behaviour for task type #%d error %d.", _task->type, _task->err.code);
    }

    return true;
}

static bool task_completed(task_t* _task)
{
    switch (_task->type)
    {
    case TASK_WT_CREATE:
    {
        if (TASK_ORIGIN_CLI == _task->origin)
            report_create(_task, stdout);
        break;
    }

    case TASK_WT_DROP:
    {
        if (TASK_ORIGIN_CLI == _task->origin)
            report_drop(_task, stdout);
        break;
    }

    case TASK_WT_DESCRIBE:
    {
        if (TASK_ORIGIN_CLI == _task->origin)
            report_describe(_task, stdout);
        break;
    }

    case TASK_WT_SELECT:
    {
        if (TASK_ORIGIN_CLI == _task->origin)
            report_select(_task, stdout);
        break;
    }

    case TASK_WT_INSERT:
    {
        if (TASK_ORIGIN_CLI == _task->origin)
            report_insert(_task, stdout);
        break;
    }

    case TASK_WT_JOURNAL:
    {
        if (TASK_ORIGIN_CLI == _task->origin)
            report_journal(_task, stdout);
        break;
    }

    case TASK_WT_ADDMEM:
    {
        if (TASK_ORIGIN_CLI == _task->origin)
            report_addmem(_task, stdout);
        break;
    }

    case TASK_WT_RUN:
    {
        data_run_t* data = _task->data;

        if (ERR_NONE == _task->err.code)
        {
            CX_INFO("script '%s' completed successfully. (%s).", data->scriptFileName, data->outputFilePath);
        }
        else
        {
            CX_INFO("script '%s' failed at line number %d. %s", data->scriptFileName, data->lineNumber - 1, _task->err.desc);
        }
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
