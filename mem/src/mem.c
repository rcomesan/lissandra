#include "mem.h"
#include "mem_worker.h"
#include "mm.h"

#include <ker/cli_parser.h>
#include <ker/reporter.h>
#include <ker/taskman.h>

#include <ker/common_protocol.h>
#include <mem/mem_protocol.h>
#include <lfs/lfs_protocol.h>
#include <ker/ker_protocol.h>

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

mem_ctx_t           g_ctx;

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
static bool         task_req_abort(task_t* _task, void* _userData);

static void         on_connected_to_lfs(cx_net_ctx_cl_t* _ctx);
static void         on_disconnected_from_lfs(cx_net_ctx_cl_t* _ctx);
static bool         on_connection(cx_net_ctx_sv_t* _ctx, const ipv4_t _ipv4);
static void         on_disconnection(cx_net_ctx_sv_t* _ctx, cx_net_client_t* _client);

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
    cx_err_t err;

    CX_MEM_ZERO(g_ctx);
    CX_ERR_CLEAR(&err);

    double timeCounter = 0;
    double timeCounterPrev = 0;
    double timeDelta = 0;

    g_ctx.shutdownReason = "main thread finished";
    g_ctx.isRunning = true
        && cx_init(PROJECT_NAME, OUTPUT_LOG_ENABLED, NULL, &err)
        && cx_timer_init(MEM_TIMER_COUNT, handle_timer_tick, &err)
        && cfg_init("res/mem.cfg", &err)
        && taskman_init(g_ctx.cfg.workers, task_run_mt, task_run_wk, task_completed, task_free, task_reschedule, &err)
        && net_init(&err)
        && mem_init(&err)
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
            cx_net_poll_events(g_ctx.sv, 0);
            cx_net_poll_events(g_ctx.lfs, 0);

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
    net_destroy();          // destroys all the communication contexts waking up & aborting all the tasks waiting on remote responses.
    taskman_destroy();      // safely destroys the pool and the blocked queues.
    mem_destroy();          // destroys memory manager.
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

        key = MEM_CFG_PASSWORD;
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

        key = MEM_CFG_MEM_NUMBER;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.memNumber = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = MEM_CFG_WORKERS;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.workers = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = MEM_CFG_LISTENING_IP;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            temp = config_get_string_value(g_ctx.cfg.handle, key);
            cx_str_copy(g_ctx.cfg.listeningIp, sizeof(g_ctx.cfg.listeningIp), temp);
        }
        else
        {
            goto key_missing;
        }

        key = MEM_CFG_LISTENING_PORT;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.listeningPort = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = MEM_CFG_LFS_IP;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            temp = config_get_string_value(g_ctx.cfg.handle, key);
            cx_str_copy(g_ctx.cfg.lfsIp, sizeof(g_ctx.cfg.lfsIp), temp);
        }
        else
        {
            goto key_missing;
        }

        key = MEM_CFG_LFS_PORT;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.lfsPort = (uint16_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = MEM_CFG_LFS_PASSWORD;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            temp = config_get_string_value(g_ctx.cfg.handle, key);

            uint32_t len = strlen(temp);
            CX_WARN(len >= MIN_PASSWD_LEN, "'%s' must have a minimum length of %d characters!", key, MIN_PASSWD_LEN);
            CX_WARN(len <= MAX_PASSWD_LEN, "'%s' must have a maximum length of %s characters!", key, MAX_PASSWD_LEN)
                if (!cx_math_in_range(len, MIN_PASSWD_LEN, MAX_PASSWD_LEN)) goto key_missing;

            cx_str_copy(g_ctx.cfg.lfsPassword, sizeof(g_ctx.cfg.lfsPassword), temp);
        }
        else
        {
            goto key_missing;
        }

        key = MEM_CFG_SEEDS_IP;
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

        key = MEM_CFG_SEEDS_PORT;
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

        key = MEM_CFG_DELAY_MEM;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.delayMem = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = MEM_CFG_DELAY_LFS;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.delayLfs = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = MEM_CFG_MEM_SIZE;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.memSize = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = MEM_CFG_INT_JOURNALING;
        if (config_has_property(g_ctx.cfg.handle, key))
        {
            g_ctx.cfg.intervalJournaling = (uint32_t)config_get_int_value(g_ctx.cfg.handle, key);
        }
        else
        {
            goto key_missing;
        }

        key = MEM_CFG_INT_GOSSIPING;
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

static bool mem_init(cx_err_t* _err)
{
    g_ctx.timerJournal = cx_timer_add(g_ctx.cfg.intervalJournaling, MEM_TIMER_JOURNAL, NULL);
    if (INVALID_HANDLE == g_ctx.timerJournal)
    {
        CX_ERR_SET(_err, ERR_INIT_TIMER, "journal timer creation failed.");
        return false;
    }

    g_ctx.timerGossip = cx_timer_add(g_ctx.cfg.intervalGossiping, MEM_TIMER_GOSSIP, NULL);
    if (INVALID_HANDLE == g_ctx.timerGossip)
    {
        CX_ERR_SET(_err, ERR_INIT_TIMER, "gossip timer creation failed.");
        return false;
    }

    return mm_init(g_ctx.cfg.memSize, g_ctx.cfg.valueSize, _err);
}

static void mem_destroy()
{
    mm_destroy();

    if (INVALID_HANDLE != g_ctx.timerJournal)
    {
        cx_timer_remove(g_ctx.timerJournal);
        g_ctx.timerJournal = INVALID_HANDLE;
    }

    if (INVALID_HANDLE != g_ctx.timerGossip)
    {
        cx_timer_remove(g_ctx.timerGossip);
        g_ctx.timerGossip = INVALID_HANDLE;
    }
}

static bool net_init(cx_err_t* _err)
{
    cx_net_args_t lfsCtxArgs;
    CX_MEM_ZERO(lfsCtxArgs);
    cx_str_copy(lfsCtxArgs.name, sizeof(lfsCtxArgs.name), "lfs");
    cx_str_copy(lfsCtxArgs.ip, sizeof(lfsCtxArgs.ip), g_ctx.cfg.lfsIp);
    lfsCtxArgs.port = g_ctx.cfg.lfsPort;
    lfsCtxArgs.multiThreadedSend = true;
    lfsCtxArgs.connectBlocking = true;
    lfsCtxArgs.connectTimeout = 15000;
    lfsCtxArgs.onConnected = (cx_net_on_connected_cb)on_connected_to_lfs;
    lfsCtxArgs.onDisconnected = (cx_net_on_connected_cb)on_disconnected_from_lfs;

    // message headers to handlers mappings
    lfsCtxArgs.msgHandlers[MEMP_ACK] = (cx_net_handler_cb)mem_handle_ack;
    lfsCtxArgs.msgHandlers[MEMP_RES_CREATE] = (cx_net_handler_cb)mem_handle_res_create;
    lfsCtxArgs.msgHandlers[MEMP_RES_DROP] = (cx_net_handler_cb)mem_handle_res_drop;
    lfsCtxArgs.msgHandlers[MEMP_RES_DESCRIBE] = (cx_net_handler_cb)mem_handle_res_describe;
    lfsCtxArgs.msgHandlers[MEMP_RES_SELECT] = (cx_net_handler_cb)mem_handle_res_select;
    lfsCtxArgs.msgHandlers[MEMP_RES_INSERT] = (cx_net_handler_cb)mem_handle_res_insert;

    // start client context
    g_ctx.lfsAvail = false;
    g_ctx.lfsHandshaking = true;
    g_ctx.lfs = cx_net_connect(&lfsCtxArgs);
    if (NULL != g_ctx.lfs && (CX_NET_STATE_CONNECTED & g_ctx.lfs->c.state))
    {
        // wait until we either get acknowledged or disconnected
        while (g_ctx.lfsHandshaking)
        {
            cx_net_poll_events(g_ctx.lfs, -1);
        }
    }

    if (!g_ctx.lfsAvail)
    {
        CX_ERR_SET(_err, ERR_INIT_NET, "could not connect to lfs server on %s:%d.",
            lfsCtxArgs.ip, lfsCtxArgs.port);
        return false;
    }
    else
    {
        CX_INFO("connection successfully established with LFS.");
    }

    cx_net_args_t svCtxArgs;
    CX_MEM_ZERO(svCtxArgs);
    cx_str_copy(svCtxArgs.name, sizeof(svCtxArgs.name), "api");
    cx_str_copy(svCtxArgs.ip, sizeof(svCtxArgs.ip), g_ctx.cfg.listeningIp);
    svCtxArgs.port = g_ctx.cfg.listeningPort;
    svCtxArgs.maxClients = 10;
    svCtxArgs.onConnection = (cx_net_on_connection_cb)on_connection;
    svCtxArgs.onDisconnection = (cx_net_on_disconnection_cb)on_disconnection;

    // message headers to handlers mappings
    svCtxArgs.msgHandlers[MEMP_AUTH] = (cx_net_handler_cb)mem_handle_auth;
    svCtxArgs.msgHandlers[MEMP_JOURNAL] = (cx_net_handler_cb)mem_handle_journal;
    svCtxArgs.msgHandlers[MEMP_REQ_CREATE] = (cx_net_handler_cb)mem_handle_req_create;
    svCtxArgs.msgHandlers[MEMP_REQ_DROP] = (cx_net_handler_cb)mem_handle_req_drop;
    svCtxArgs.msgHandlers[MEMP_REQ_DESCRIBE] = (cx_net_handler_cb)mem_handle_req_describe;
    svCtxArgs.msgHandlers[MEMP_REQ_SELECT] = (cx_net_handler_cb)mem_handle_req_select;
    svCtxArgs.msgHandlers[MEMP_REQ_INSERT] = (cx_net_handler_cb)mem_handle_req_insert;

    // start server context
    g_ctx.sv = cx_net_listen(&svCtxArgs);
    if (NULL == g_ctx.sv)
    {
        CX_ERR_SET(_err, ERR_INIT_NET, "could not start a listening server context on %s:%d.",
            svCtxArgs.ip, svCtxArgs.port);
        return false;
    }

    return true;
}

static void net_destroy()
{
    cx_net_destroy(g_ctx.sv);
    g_ctx.sv = NULL;

    cx_net_destroy(g_ctx.lfs);
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
    else if (strcmp("JOURNAL", _cmd->header) == 0)
    {
        packetSize = mem_pack_journal(g_ctx.buff1, sizeof(g_ctx.buff1));
        mem_handle_journal((cx_net_common_t*)g_ctx.sv, NULL, g_ctx.buff1, packetSize);
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
    case MEM_TIMER_JOURNAL:
    {
        // enqueue a journal request. 
        task_t* task = taskman_create(TASK_ORIGIN_INTERNAL_PRIORITY, TASK_MT_JOURNAL, NULL, NULL);
        if (NULL != task)
        {
            task->state = TASK_STATE_NEW;
        }
        else
        {
            CX_WARN(CX_ALW, "MEM_TIMER_JOURNAL tick ignored. we ran out of tasks handles!");
        }
        break;
    }

    case MEM_TIMER_GOSSIP:
    {
        //TODO
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

    case TASK_WT_JOURNAL:
    {
        worker_handle_journal(_task);
        break;
    }

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
    segment_t*  table = NULL;
    task_t*     task = NULL;

    switch (_task->type)
    {

    case TASK_MT_JOURNAL:
    {
        success = mm_journal_tryenqueue();
        break;
    }

    case TASK_MT_FREE:
    {
        data_free_t* data = _task->data;
        
        if (RESOURCE_TYPE_TABLE == data->resourceType)
        {
            table = (segment_t*)data->resourcePtr;

            if (0 == cx_reslock_counter(&table->reslock))
            {
                // at this point the table no longer exist (it's not part of the tablesMap dictionary)
                // and nobody else is using it... we can destroy & deallocate this segment now.
                mm_segment_destroy(table);
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
    if (ERR_MEMORY_FULL == _task->err.code 
        || ERR_MEMORY_BLOCKED == _task->err.code
        || ERR_TABLE_BLOCKED == _task->err.code)
    {
        mm_reschedule_task(_task);
    }
    else
    {
        CX_WARN(CX_ALW, "undefined <reschedule> behaviour for task type #%d error %d.", _task->type, _task->err.code);
    }

    return true;
}

static bool task_req_abort(task_t* _task, void* _userData)
{
    pthread_mutex_lock(&_task->responseMtx);
    if (TASK_STATE_RUNNING_AWAITING == _task->state)
    {
        _task->state = TASK_STATE_RUNNING;
        CX_ERR_SET(&_task->err, ERR_NET_LFS_UNAVAILABLE, "LFS node is unavailable.");
        pthread_cond_signal(&_task->responseCond);
    }
    pthread_mutex_unlock(&_task->responseMtx);
    
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
            report_create(_task, stdout);
        break;
    }

    case TASK_WT_DROP:
    {
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

    case TASK_WT_JOURNAL:
    {
        double blockedTime = 0;
        mm_unblock(&blockedTime);

        if (ERR_NONE == _task->err.code)
        {
            CX_INFO("memory journal completed successfully. (%.3f sec blocked)\n", blockedTime);
        }
        else
        {
            CX_INFO("memory journal failed. %s\n", _task->err.desc);
        }
        break;
    }

    case TASK_MT_JOURNAL:
    {
        if (TASK_ORIGIN_CLI == _task->origin)
            report_info("Memory journal scheduled.", stdout);
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

    case TASK_WT_JOURNAL:
    {
        //noop
        break;
    }

    case TASK_MT_JOURNAL:
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

static void on_connected_to_lfs(cx_net_ctx_cl_t* _ctx)
{
    payload_t payload;
    uint32_t payloadSize = lfs_pack_auth(payload, sizeof(payload), g_ctx.cfg.lfsPassword);
    
    cx_net_send(_ctx, LFSP_AUTH, payload, payloadSize, INVALID_HANDLE);
}

static void on_disconnected_from_lfs(cx_net_ctx_cl_t* _ctx)
{
    if (g_ctx.lfsHandshaking)
    {
        g_ctx.lfsAvail = false;
        g_ctx.lfsHandshaking = false;
    }
    else if (g_ctx.lfsAvail)
    {
        g_ctx.lfsAvail = false;
        g_ctx.isRunning = false;
        g_ctx.shutdownReason = "lfs is unavailable";

        // the connection with the LFS node is gone. let's wake up all the tasks with pending requests on it.
        taskman_foreach((taskman_func_cb)task_req_abort, NULL);
    }
}

static bool on_connection(cx_net_ctx_sv_t* _ctx, const ipv4_t _ipv4)
{
    return g_ctx.isRunning;
}

static void on_disconnection(cx_net_ctx_sv_t* _ctx, cx_net_client_t* _client)
{
    // KER or MEM node just disconnected.
}

static void api_response_create(const task_t* _task)
{
    uint32_t payloadSize = ker_pack_res_create(g_ctx.buff1, sizeof(g_ctx.buff1),
        _task->remoteId, &_task->err);

    cx_net_send(g_ctx.sv, KERP_RES_CREATE, g_ctx.buff1, payloadSize, _task->clientHandle);
}

static void api_response_drop(const task_t* _task)
{
    uint32_t payloadSize = ker_pack_res_drop(g_ctx.buff1, sizeof(g_ctx.buff1),
        _task->remoteId, &_task->err);

    cx_net_send(g_ctx.sv, KERP_RES_DROP, g_ctx.buff1, payloadSize, _task->clientHandle);
}

static void api_response_describe(const task_t* _task)
{
    data_describe_t* data = _task->data;
    uint32_t pos = 0;
    uint16_t tablesPacked = 0;

    while (!common_pack_res_describe(g_ctx.buff1, sizeof(g_ctx.buff1), &pos,
        _task->remoteId, data->tables, data->tablesCount, &tablesPacked, &_task->err))
    {
        cx_net_send(g_ctx.sv, KERP_RES_DESCRIBE, g_ctx.buff1, pos, _task->clientHandle);
    }

    if (pos > sizeof(uint16_t))
    {
        cx_net_send(g_ctx.sv, KERP_RES_DESCRIBE, g_ctx.buff1, pos, _task->clientHandle);
    }
}

static void api_response_select(const task_t* _task)
{
    data_select_t* data = _task->data;
    uint32_t payloadSize = ker_pack_res_select(g_ctx.buff1, sizeof(g_ctx.buff1),
        _task->remoteId, &_task->err, &data->record);

    cx_net_send(g_ctx.sv, KERP_RES_SELECT, g_ctx.buff1, payloadSize, _task->clientHandle);
}

static void api_response_insert(const task_t* _task)
{
    uint32_t payloadSize = ker_pack_res_insert(g_ctx.buff1, sizeof(g_ctx.buff1),
        _task->remoteId, &_task->err);

    cx_net_send(g_ctx.sv, KERP_RES_INSERT, g_ctx.buff1, payloadSize, _task->clientHandle);
}
