#include "ker.h"
#include "ker_worker.h"
#include "mempool.h"

#include <ker/cli_parser.h>
#include <ker/reporter.h>
#include <ker/taskman.h>
#include <ker/common.h>
#include <ker/ker_protocol.h>
#include <mem/mem_protocol.h>

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/file.h>
#include <cx/net.h>
#include <cx/str.h>
#include <cx/math.h>
#include <cx/timer.h>

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

static bool         cfg_init(const char* _cfgFilePath, cx_err_t* _err);
static void         cfg_destroy();

static bool         ker_init(cx_err_t* _err);
static void         ker_destroy();

static void         handle_cli_command(const cx_cli_cmd_t* _cmd);
static bool         handle_timer_tick(uint64_t _expirations, uint32_t _id, void* _userData);

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
        && cfg_init((_argc > 1) ? _argv[1] : "res/ker.cfg", &err)
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
            mempool_update();

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
    cx_cli_destroy();       // destroys command line interface.
    taskman_stop();         // denies new tasks creation & pauses the pool so that current enqueued tasks can't start executing.
    mempool_disconnect();   // destroys all the communication contexts. wakes up & aborts all the tasks waiting on remote responses.
    taskman_destroy();      // safely destroys the pool and the blocked queues.
    ker_destroy();          // destroys mempool and timers.
    cfg_destroy();          // destroys config.
    cx_timer_destroy();     // destroys timers.

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

static bool cfg_init(const char* _cfgFilePath, cx_err_t* _err)
{
    char* temp = NULL;
    char* key = "";

    cx_path_t cfgPath;
    cx_file_path(&cfgPath, "%s", _cfgFilePath);

    g_ctx.cfg.handle = config_create(cfgPath);

    if (NULL != g_ctx.cfg.handle)
    {
        CX_INFO("config file: %s", cfgPath);

        key = KER_CFG_WORKERS;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.workers = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = KER_CFG_QUANTUM;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.quantum = (uint8_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = KER_CFG_MEM_NUMBER;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.memNumber = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = KER_CFG_MEM_IP;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            temp = config_get_string_value(g_ctx.cfg.handle, key);
            cx_str_copy(g_ctx.cfg.memIp, sizeof(g_ctx.cfg.memIp), temp);
        }
        else
        {
            goto key_missing;
        }

        key = KER_CFG_MEM_PORT;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.memPort = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = KER_CFG_MEM_PASSWORD;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            temp = config_get_string_value(g_ctx.cfg.handle, key);

            uint32_t len = strlen(temp);
            CX_WARN(len >= MIN_PASSWD_LEN, "'%s' must have a minimum length of %d characters!", key, MIN_PASSWD_LEN);
            CX_WARN(len <= MAX_PASSWD_LEN, "'%s' must have a maximum length of %s characters!", key, MAX_PASSWD_LEN)
                if (!cx_math_in_range(len, MIN_PASSWD_LEN, MAX_PASSWD_LEN)) goto key_missing;

            cx_str_copy(g_ctx.cfg.memPassword, sizeof(g_ctx.cfg.memPassword), temp);
        }
        else
        {
            goto key_missing;
        }

        key = KER_CFG_DELAY_RUN;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.delayRun = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = KER_CFG_INT_METAREFRESH;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.intervalMetaRefresh = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        return true;

    key_missing:
        CX_ERR_SET(_err, ERR_CFG_MISSINGKEY, "key '%s' is either missing or invalid in the configuration file.", key);
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

static bool ker_init(cx_err_t* _err)
{
    g_ctx.timerMetaRefresh = cx_timer_add(g_ctx.cfg.intervalMetaRefresh, KER_TIMER_METAREFRESH, NULL);
    if (INVALID_HANDLE == g_ctx.timerMetaRefresh)
    {
        CX_ERR_SET(_err, ERR_INIT_TIMER, "metarefresh timer creation failed.");
        return false;
    }

    if (mempool_init(_err))
    {
        mempool_add(g_ctx.cfg.memNumber, g_ctx.cfg.memIp, g_ctx.cfg.memPort);
        return true;
    }

    return false;
}

static void ker_destroy()
{
    mempool_destroy();

    if (INVALID_HANDLE != g_ctx.timerMetaRefresh)
    {
        cx_timer_remove(g_ctx.timerMetaRefresh);
        g_ctx.timerMetaRefresh = INVALID_HANDLE;
    }
}

static void handle_cli_command(const cx_cli_cmd_t* _cmd)
{
    cx_err_t  err;
    CX_MEM_ZERO(err);

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

    if (strcmp("EXIT", _cmd->header) == 0)
    {
        g_ctx.isRunning = false;
        g_ctx.shutdownReason = "cli-issued exit";
    }
    else if (strcmp("LOGFILE", _cmd->header) == 0)
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
    else if (strcmp("CREATE", _cmd->header) == 0)
    {
        if (cli_parse_create(_cmd, &err, &tableName, &consistency, &numPartitions, &compactionInterval))
        {
            packetSize = ker_pack_req_create(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, consistency, numPartitions, compactionInterval);
            ker_handle_req_create(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("DROP", _cmd->header) == 0)
    {
        if (cli_parse_drop(_cmd, &err, &tableName))
        {
            packetSize = ker_pack_req_drop(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName);
            ker_handle_req_drop(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("DESCRIBE", _cmd->header) == 0)
    {
        if (cli_parse_describe(_cmd, &err, &tableName))
        {
            packetSize = ker_pack_req_describe(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName);
            ker_handle_req_describe(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("SELECT", _cmd->header) == 0)
    {
        if (cli_parse_select(_cmd, &err, &tableName, &key))
        {
            packetSize = ker_pack_req_select(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, key);
            ker_handle_req_select(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("INSERT", _cmd->header) == 0)
    {
        if (cli_parse_insert(_cmd, &err, &tableName, &key, &value, &timestamp))
        {
            packetSize = ker_pack_req_insert(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, key, value, timestamp);
            ker_handle_req_insert(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("JOURNAL", _cmd->header) == 0)
    {
        packetSize = ker_pack_req_journal(g_ctx.buff1, sizeof(g_ctx.buff1), 0);
        ker_handle_req_journal(NULL, NULL, g_ctx.buff1, packetSize);
    }
    else if (strcmp("ADD", _cmd->header) == 0)
    {
        if (cli_parse_add_memory(_cmd, &err, &memNumber, &consistency))
        {
            packetSize = ker_pack_req_addmem(g_ctx.buff1, sizeof(g_ctx.buff1), 0, memNumber, consistency);
            ker_handle_req_addmem(NULL, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("RUN", _cmd->header) == 0)
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

        // for now, we'll just keep adding our known MEM node over and over
        mempool_add(g_ctx.cfg.memNumber, g_ctx.cfg.memIp, g_ctx.cfg.memPort);

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

    default:
        CX_WARN(CX_ALW, "undefined <tick> behaviour for timer of type #%d.", _type);
        break;
    }

    return !stopTimer;
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

        cx_path_t fileName;
        cx_file_get_name(&data->scriptFilePath, false, &fileName);

        if (ERR_NONE == _task->err.code)
        {
            CX_INFO("script '%s' completed successfully. (%s).", fileName, data->outputFilePath);
        }
        else
        {
            CX_INFO("script '%s' failed at line number %d. %s", fileName, data->lineNumber - 1, _task->err.desc);
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
    return task_data_free(_task->type, _task->data);
}
