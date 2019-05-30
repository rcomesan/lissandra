#include "mem.h"
#include "mem_worker.h"

#include <ker/cli_parser.h>
#include <ker/cli_reporter.h>
#include <ker/taskman.h>
#include <ker/logger.h>

#include <mem/mem_protocol.h>
#include <lfs/lfs_protocol.h>

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/file.h>
#include <cx/net.h>
#include <cx/str.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

mem_ctx_t           g_ctx;                                  // global MEM context

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool         cfg_init(const char* _cfgFilePath, cx_err_t* _err);
static void         cfg_destroy();

static bool         mem_init(cx_err_t* _err);
static void         mem_destroy();

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
    char* shutdownReason = "main thread finished";

    cx_err_t err;

    g_ctx.isRunning = true
        && cx_timer_init(1, handle_timer_tick, &err)
        && logger_init(&g_ctx.log, &err)
        && cfg_init("res/mem.cfg", &err)
        && taskman_init(g_ctx.cfg.workers, task_run_mt, task_run_wk, task_completed, task_free, task_reschedule, &err)
        && mem_init(&err)
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
            cx_net_poll_events(g_ctx.sv);
            cx_net_poll_events(g_ctx.lfs);

            if (!(CX_NET_STATE_CONNECTING & g_ctx.lfs->c.state) 
                && !(CX_NET_STATE_CONNECTED & g_ctx.lfs->c.state))
            {
                shutdownReason = "lfs is unavailable";
                g_ctx.isRunning = false;
            }

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
        shutdownReason = "initialization failed";
        log_error(g_ctx.log, "initialization failed (errcode %d). %s", err.code, err.desc);
    }
    else
    {
        shutdownReason = "fatal error";
        printf("[FATAL] (errcode %d) %s", err.code, err.desc);
    }

    CX_INFO("node is shutting down. reason: %s.", shutdownReason);
    cx_cli_destroy();
    net_destroy();
    mem_destroy();
    taskman_destroy();
    cfg_destroy();
    cx_timer_destroy();

    if (0 == err.code)
        CX_INFO("node terminated gracefully.");
    else
        CX_INFO("node terminated with error %d.", err.code);

    logger_destroy(g_ctx.log);
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

        key = "memNumber";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.memNumber = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
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

        key = "lfsIp";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            temp = config_get_string_value(g_ctx.cfg.handle, key);
            cx_str_copy(g_ctx.cfg.lfsIp, sizeof(g_ctx.cfg.lfsIp), temp);
        }
        else
        {
            goto key_missing;
        }

        key = "lfsPort";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.lfsPort = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = "seedsIp";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            char** ips = config_get_array_value(g_ctx.cfg.handle, key);

            uint32_t i = 0;
            bool finished = (NULL == ips[i]);
            while (!finished && g_ctx.cfg.seedsCount < MAX_MEM_SEEDS)
            {
                cx_str_copy(g_ctx.cfg.seedsIps[g_ctx.cfg.seedsCount++], sizeof(ipv4_t), ips[i]);
                free(ips[i++]);
                finished = (NULL == ips[i]);
            }
            free(ips);
            CX_CHECK(finished, "some seeds ip addresses were not read! static buffer of %d elements is not enough!", MAX_MEM_SEEDS);
        }
        else
        {
            goto key_missing;
        }

        key = "seedsPort";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            char** ports = config_get_array_value(g_ctx.cfg.handle, key);

            uint32_t i = 0, j = 0;
            bool finished = (NULL == ports[i]);
            while (!finished && j < g_ctx.cfg.seedsCount)
            {
                cx_str_to_uint16(ports[i], &(g_ctx.cfg.seedsPorts[j++]));
                free(ports[i++]);
                finished = (NULL == ports[i]);
            }
            free(ports);
            CX_WARN(j == g_ctx.cfg.seedsCount, "seedsIp and seedsPort arrays do not match! (number of seeds loaded: %d)", j)
            g_ctx.cfg.seedsCount = j;
        }
        else
        {
            goto key_missing;
        }

        key = "delayMem";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.delayMem = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = "delayLfs";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.delayLfs = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = "memSize";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.memSize = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = "intervalJournaling";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.intervalJournaling = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = "intervalGossiping";
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.intervalGossiping = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
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

static bool mem_init(cx_err_t* _err)
{
    return true;
}

static void mem_destroy()
{

}

static bool net_init(cx_err_t* _err)
{
    cx_net_args_t svCtxArgs;
    CX_MEM_ZERO(svCtxArgs);
    cx_str_copy(svCtxArgs.name, sizeof(svCtxArgs.name), "api");
    cx_str_copy(svCtxArgs.ip, sizeof(svCtxArgs.ip), g_ctx.cfg.listeningIp);
    svCtxArgs.port = g_ctx.cfg.listeningPort;

    // message headers to handlers mappings
    svCtxArgs.msgHandlers[MEMP_REQ_CREATE] = (cx_net_handler_cb*)mem_handle_req_create;
    svCtxArgs.msgHandlers[MEMP_REQ_DROP] = (cx_net_handler_cb*)mem_handle_req_drop;
    svCtxArgs.msgHandlers[MEMP_REQ_DESCRIBE] = (cx_net_handler_cb*)mem_handle_req_describe;
    svCtxArgs.msgHandlers[MEMP_REQ_SELECT] = (cx_net_handler_cb*)mem_handle_req_select;
    svCtxArgs.msgHandlers[MEMP_REQ_INSERT] = (cx_net_handler_cb*)mem_handle_req_insert;

    // start server context and start listening for requests
    g_ctx.sv = cx_net_listen(&svCtxArgs);
    if (NULL == g_ctx.sv)
    {
        CX_ERR_SET(_err, ERR_NET_FAILED, "could not start a listening server context on %s:%d.",
            svCtxArgs.ip, svCtxArgs.port);
        return false;
    }

    cx_net_args_t lfsCtxArgs;
    CX_MEM_ZERO(lfsCtxArgs);
    cx_str_copy(lfsCtxArgs.name, sizeof(lfsCtxArgs.name), "lfs");
    cx_str_copy(lfsCtxArgs.ip, sizeof(lfsCtxArgs.ip), g_ctx.cfg.lfsIp);
    lfsCtxArgs.port = g_ctx.cfg.lfsPort;
    lfsCtxArgs.multiThreadedSend = true;

    // message headers to handlers mappings
    lfsCtxArgs.msgHandlers[MEMP_RES_CREATE] = (cx_net_handler_cb*)mem_handle_res_create;
    lfsCtxArgs.msgHandlers[MEMP_RES_DROP] = (cx_net_handler_cb*)mem_handle_res_drop;
    lfsCtxArgs.msgHandlers[MEMP_RES_DESCRIBE] = (cx_net_handler_cb*)mem_handle_res_describe;
    lfsCtxArgs.msgHandlers[MEMP_RES_SELECT] = (cx_net_handler_cb*)mem_handle_res_select;
    lfsCtxArgs.msgHandlers[MEMP_RES_INSERT] = (cx_net_handler_cb*)mem_handle_res_insert;

    // start server context and start listening for requests
    g_ctx.lfs = cx_net_connect(&lfsCtxArgs);
    if (NULL == g_ctx.lfs)
    {
        CX_ERR_SET(_err, ERR_NET_FAILED, "could not connect to lfs server on %s:%d.",
            lfsCtxArgs.ip, lfsCtxArgs.port);
        return false;
    }

    return true;
}

static void net_destroy()
{
    //TODO make sure there're no pending requests from KER node.
    // we need to notify them in some way that we're shutting down!
    
    //TODO make sure to wake up all the threads waiting for LFS

    cx_net_close(g_ctx.sv);
    cx_net_close(g_ctx.lfs);
    g_ctx.sv = NULL;
    g_ctx.lfs = NULL;
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
            packetSize = mem_pack_req_create(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, consistency, numPartitions, compactionInterval);
            mem_handle_req_create((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("DROP", _cmd->header) == 0)
    {
        if (cli_parse_drop(_cmd, &err, &tableName))
        {
            packetSize = mem_pack_req_drop(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName);
            mem_handle_req_drop((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("DESCRIBE", _cmd->header) == 0)
    {
        if (cli_parse_describe(_cmd, &err, &tableName))
        {
            packetSize = mem_pack_req_describe(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName);
            mem_handle_req_describe((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("SELECT", _cmd->header) == 0)
    {
        if (cli_parse_select(_cmd, &err, &tableName, &key))
        {
            packetSize = mem_pack_req_select(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, key);
            mem_handle_req_select((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
    }
    else if (strcmp("INSERT", _cmd->header) == 0)
    {
        if (cli_parse_insert(_cmd, &err, &tableName, &key, &value, &timestamp))
        {
            packetSize = mem_pack_req_insert(g_ctx.buff1, sizeof(g_ctx.buff1), 0, tableName, key, value, timestamp);
            mem_handle_req_insert((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
        }
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
    task_t*  task = NULL;

    switch (_task->type)
    {

    default:
        CX_WARN(CX_ALW, "undefined <main-thread> behaviour for task type #%d.", _task->type);
        break;
    }

    return success;
}

static bool task_reschedule(task_t* _task)
{
    return true;
}

static bool task_completed(task_t* _task)
{
    switch (_task->type)
    {
    case TASK_WT_CREATE:
    {
        if (TASK_ORIGIN_API == _task->origin)
            api_response_create(_task);
        else
            cli_report_create(_task);
        break;
    }

    case TASK_WT_DROP:
    {
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

    default:
        CX_WARN(CX_ALW, "undefined <free> behaviour for request type #%d.", _task->type);
        break;
    }

    return true;
}

static void api_response_create(const task_t* _task)
{
    //TODO. reply back to the KER node that requested this query.
}

static void api_response_drop(const task_t* _task)
{
    //TODO. reply back to the KER node that requested this query.
}

static void api_response_describe(const task_t* _task)
{
    //TODO. reply back to the KER node that requested this query.
}

static void api_response_select(const task_t* _task)
{
    //TODO. reply back to the KER node that requested this query.
}

static void api_response_insert(const task_t* _task)
{
    //TODO. reply back to the KER node that requested this query.
}
