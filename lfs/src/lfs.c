#include "lfs.h"
#include "memtable.h"
#include "worker.h"

#include <ker/cli_parser.h>
#include <ker/cli_reporter.h>

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/file.h>
#include <cx/str.h>
#include <cx/cli.h>
#include <cx/fs.h>

#include <lfs/lfs_protocol.h>
#include <commons/config.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

lfs_ctx_t           g_ctx;                                  // global LFS context
static uint16_t     m_auxHandles[MAX_CONCURRENT_REQUESTS];  // statically allocated aux buffer for storing handles

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool         logger_init(cx_error_t* _err);
static void         logger_destroy();

static bool         cfg_init(const char* _cfgFilePath, cx_error_t* _err);
static void         cfg_destroy();

static bool         lfs_init(cx_error_t* _err);
static void         lfs_destroy();

static bool         net_init(cx_error_t* _err);
static void         net_destroy();

static bool         cli_init();
static void         cli_destroy();

static void         handle_cli_command(const cx_cli_cmd_t* _cmd);
static void         handle_worker_task(request_t* _req);

static void         requests_update();
static void         request_completed(const request_t* _req);
static void         request_free(request_t* _req);

static void         api_response_create(const data_create_t* _data);
static void         api_response_drop(const data_drop_t* _data);
static void         api_response_describe(const data_describe_t* _data);
static void         api_response_select(const data_select_t* _data);
static void         api_response_insert(const data_insert_t* _data);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

int main(int _argc, char** _argv)
{
    cx_init(PROJECT_NAME);
    CX_MEM_ZERO(g_ctx);

    cx_error_t err;

    g_ctx.isRunning = true
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

            // update containers
            requests_update();
        }
    }
    else if (NULL != g_ctx.log)
    {
        log_error(g_ctx.log, "Initialization failed (errcode %d). %s", err.code, err.desc);
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

    if (0 == err.code)
        CX_INFO("node terminated successfully.");
    else
        CX_INFO("node terminated with error %d.", err.code);
    
    logger_destroy();
    return err.code;
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static bool logger_init(cx_error_t* _err)
{
    cx_timestamp_t timestamp;
    cx_time_stamp(timestamp);

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

        CX_ERROR_SET(_err, LFS_ERR_LOGGER_FAILED, "%s log initialization failed (%s).", PROJECT_NAME, path);
    }
    else
    {
        CX_ERROR_SET(_err, LFS_ERR_LOGGER_FAILED, "%s logs folder creation failed (%s).", PROJECT_NAME, path);
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

static bool cfg_init(const char* _cfgFilePath, cx_error_t* _err)
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
            config_get_string_value(g_ctx.cfg.handle, key);
            cx_str_copy(g_ctx.cfg.rootDir, sizeof(g_ctx.cfg.rootDir), temp);
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
         CX_ERROR_SET(_err, LFS_ERR_CFG_MISSINGKEY, "Key '%s' is missing in the configuration file.", key);
         return false;
    }
    else
    {
        CX_ERROR_SET(_err, LFS_ERR_CFG_NOTFOUND, "Configuration file '%s' is missing or not readable.", cfgPath);
        return false;
    }
}

static void cfg_destroy()
{
    if (NULL != g_ctx.cfg.handle)
    {
        config_destroy(g_ctx.cfg.handle);
        g_ctx.cfg.handle = NULL;
    }
}

static bool lfs_init(cx_error_t* _err)
{
    uint8_t stage = 0;

    g_ctx.requestsHalloc = cx_halloc_init(MAX_CONCURRENT_REQUESTS);
    if (NULL == g_ctx.requestsHalloc)
    {
        CX_ERROR_SET(_err, LFS_ERR_INIT_HALLOC, "requests handle allocator creation failed.");
        return false;
    }

    g_ctx.pool = cx_pool_init("worker", g_ctx.cfg.workers, (cx_pool_handler_cb)handle_worker_task);
    if (NULL == g_ctx.pool)
    {
        CX_ERROR_SET(_err, LFS_ERR_INIT_THREADPOOL, "thread pool creation failed.");
        return false;
    }

    return true;
}

static void lfs_destroy()
{
    cx_halloc_destroy(g_ctx.requestsHalloc);
    g_ctx.requestsHalloc = NULL;

    cx_pool_destroy(g_ctx.pool);
    g_ctx.pool = NULL;
}

static bool net_init(cx_error_t* _err)
{
    cx_net_args_t svCtxArgs;
    CX_MEM_ZERO(svCtxArgs);
    cx_str_copy(svCtxArgs.name, sizeof(svCtxArgs.name), "api");
    cx_str_copy(svCtxArgs.ip, sizeof(svCtxArgs.ip), g_ctx.cfg.listeningIp);
    svCtxArgs.port = g_ctx.cfg.listeningPort;

    // message headers to handlers mappings
    svCtxArgs.msgHandlers[LFSP_SUM_REQUEST] = lfs_handle_sum_request;

    // start server context and start listening for requests
    g_ctx.sv = cx_net_listen(&svCtxArgs);

    if (NULL != g_ctx.sv)
    {
        return true;
    }
    else
    {
        CX_ERROR_SET(_err, LFS_ERR_NET_FAILED, "Could not start a listening server context on %s:%d.",
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
    cx_error_t  err;
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
    else
    {
        CX_ERROR_SET(&err, 1, "Unknown command '%s'.", _cmd->header);
    }

    if (0 != err.code)
    {
        cli_report_error(&err);
        cx_cli_command_end();
    }
}

static void handle_worker_task(request_t* _req)
{
    REQ_STATE r;
    _req->state = REQ_STATE_RUNNING;

    switch (_req->type)
    {
    case REQ_TYPE_CREATE:
        worker_handle_create(_req);
        break;

    case REQ_TYPE_DROP:
        worker_handle_drop(_req);
        break;

    case REQ_TYPE_DESCRIBE:
        worker_handle_describe(_req);
        break;

    case REQ_TYPE_SELECT:
        worker_handle_select(_req);
        break;

    case REQ_TYPE_INSERT:
        worker_handle_insert(_req);
        break;

    default:
        CX_WARN(CX_ALW, "Undefined worker behaviour for request type #%d.", _req->type);
        break;
    }
}

void dump_memtables()
{
    //dictionary_iterator(g_ctx.tables, memtable_dump);
    //dictionary_clean_and_destroy_elements(g_ctx.tables, memtable_destroy);
}

void requests_update()
{
    uint16_t max = cx_handle_count(g_ctx.requestsHalloc);
    uint16_t handle = INVALID_HANDLE;
    request_t* req = NULL;
    uint16_t removeCount = 0;
    data_common_t* dataCommon = NULL;

    for (uint16_t i = 0; i < max; i++)
    {
        handle = cx_handle_at(g_ctx.requestsHalloc, i);
        req = &(g_ctx.requests[handle]);

        if (REQ_STATE_NEW == req->state)
        {
            dataCommon = req->data;
            dataCommon->startTime = cx_time_counter();
            dataCommon->success = false;

            cx_pool_submit(g_ctx.pool, req);
        }
        else if (REQ_STATE_COMPLETED == req->state)
        {
            request_completed(req);
            request_free(req);
            m_auxHandles[removeCount++] = handle;
        }
        else /* TODO handle requests blocked here. re-schedule them into our blocked queue for delayed execution */
        {
        }
    }

    for (uint16_t i = 0; i < removeCount; i++)
    {   
        handle = m_auxHandles[i];
        req = &(g_ctx.requests[handle]);
        cx_handle_free(g_ctx.requestsHalloc, handle);
    }
}

static void request_completed(const request_t* _req)
{
    switch (_req->type)
    {
    case REQ_TYPE_CREATE:
        if (REQ_ORIGIN_API == _req->origin)
            api_response_create((data_create_t*)_req->data);
        else
            cli_report_create((data_create_t*)_req->data);
        break;

    case REQ_TYPE_DROP:
        if (REQ_ORIGIN_API == _req->origin)
            api_response_drop((data_drop_t*)_req->data);
        else
            cli_report_drop((data_drop_t*)_req->data);
        break;

    case REQ_TYPE_DESCRIBE:
        if (REQ_ORIGIN_API == _req->origin)
            api_response_describe((data_create_t*)_req->data);
        else
            cli_report_describe((data_create_t*)_req->data);
        break;

    case REQ_TYPE_SELECT:
        if (REQ_ORIGIN_API == _req->origin)
            api_response_select((data_select_t*)_req->data);
        else
            cli_report_select((data_select_t*)_req->data);
        break;

    case REQ_TYPE_INSERT:
        if (REQ_ORIGIN_API == _req->origin)
            api_response_insert((data_insert_t*)_req->data);
        else
            cli_report_insert((data_insert_t*)_req->data);
        break;

    default:
        CX_WARN(CX_ALW, "Undefined <completed> behaviour for request type #%d", _req->type);
        break;
    }

    if (REQ_ORIGIN_CLI == _req->origin)
    {
        // mark the current (processed) command as done
        cx_cli_command_end();
    }
}

static void request_free(request_t* _req)
{
    switch (_req->type)
    {
    case REQ_TYPE_CREATE:
    {
        data_create_t* data = (data_create_t*)_req->data;
        break;
    }

    case REQ_TYPE_DROP:
    {
        data_drop_t* data = (data_drop_t*)_req->data;
        break;
    }

    case REQ_TYPE_DESCRIBE:
    {
        data_describe_t* data = (data_describe_t*)_req->data;
        free(data->tables);
        data->tables = NULL;
        data->tablesCount = 0;
        break;
    }

    case REQ_TYPE_SELECT:
    {
        data_select_t* data = (data_select_t*)_req->data;
        free(data->value);
        data->value = NULL;
        break;
    }

    case REQ_TYPE_INSERT:
    {
        data_insert_t* data = (data_insert_t*)_req->data;
        free(data->value);
        data->value = NULL;
        break;
    }

    default:
        CX_WARN(CX_ALW, "Undefined <free> behaviour for request type #%d", _req->type);
        break;
    }

    free(_req->data);
    
    // note that requests are statically allocated. we don't want to free them
    // here since we'll keep reusing them in subsequent requests.
    CX_MEM_ZERO(*_req)
}

static void api_response_create(const data_create_t* _result)
{
    //TODO. reply back to the MEM node that requested this query.
}

static void api_response_drop(const data_drop_t* _result)
{
    //TODO. reply back to the MEM node that requested this query.
}

static void api_response_describe(const data_describe_t* _result)
{
    //TODO. reply back to the MEM node that requested this query.
}

static void api_response_select(const data_select_t* _result)
{
    //TODO. reply back to the MEM node that requested this query.
}

static void api_response_insert(const data_insert_t* _result)
{
    //TODO. reply back to the MEM node that requested this query.
}