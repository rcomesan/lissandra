#include "ker.h"
#include "ker_worker.h"
#include "mempool.h"

#include <ker/defines.h>
#include <ker/cli_parser.h>
#include <ker/common.h>
#include <ker/reporter.h>
#include <mem/mem_protocol.h>

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/str.h>
#include <cx/file.h>
#include <cx/timer.h>
#include <cx/cli.h>

#include <unistd.h>

/****************************************************************************************
***  PRIVATE DECLARATIONS
***************************************************************************************/

static void         _worker_parse_result(task_t* _req);

static bool         _worker_request_mem(mempool_hints_t* _hints, uint8_t _header, const char* _payload, uint32_t _payloadSize, task_t* _task);

static bool         _worker_run_query_scripted(cx_cli_cmd_t* _cmd, task_t* _task);

/****************************************************************************************
***  PUBLIC FUNCTIONS
***************************************************************************************/

void worker_handle_create(task_t* _req, bool _scripted)
{
    data_create_t* data = _req->data;

    mempool_hints_t hints;
    CX_MEM_ZERO(hints);
    hints.query = QUERY_CREATE;
    hints.tableName = data->tableName;
    hints.consistency = (E_CONSISTENCY)data->consistency;

    payload_t payload;
    uint32_t payloadSize = mem_pack_req_create(payload, sizeof(payload),
        _req->handle, data->tableName, data->consistency,
        data->numPartitions, data->compactionInterval);

    _worker_request_mem(&hints, MEMP_REQ_CREATE, payload, payloadSize, _req);

    if (!_scripted) _worker_parse_result(_req);
}

void worker_handle_drop(task_t* _req, bool _scripted)
{
    data_drop_t* data = _req->data;

    mempool_hints_t hints;
    CX_MEM_ZERO(hints);
    hints.query = QUERY_DROP;
    hints.tableName = data->tableName;

    payload_t payload;
    uint32_t payloadSize = mem_pack_req_drop(payload, sizeof(payload),
        _req->handle, data->tableName);

    _worker_request_mem(&hints, MEMP_REQ_DROP, payload, payloadSize, _req);

    if (!_scripted) _worker_parse_result(_req);
}

void worker_handle_describe(task_t* _req, bool _scripted)
{
    data_describe_t* data = _req->data;
    const char* tableName = NULL;

    if (data->tablesCount == 1)
        tableName = data->tables[0].name;

    mempool_hints_t hints;
    CX_MEM_ZERO(hints);
    hints.query = QUERY_DESCRIBE;
    hints.tableName = tableName;

    payload_t payload;
    uint32_t payloadSize = mem_pack_req_describe(payload, sizeof(payload),
        _req->handle, tableName);

    _worker_request_mem(&hints, MEMP_REQ_DESCRIBE, payload, payloadSize, _req);

    if (!_scripted) _worker_parse_result(_req);
}

void worker_handle_select(task_t* _req, bool _scripted)
{
    data_select_t* data = _req->data;

    mempool_hints_t hints;
    CX_MEM_ZERO(hints);
    hints.query = QUERY_SELECT;
    hints.tableName = data->tableName;
    hints.key = data->record.key;

    payload_t payload;
    uint32_t payloadSize = mem_pack_req_select(payload, sizeof(payload),
        _req->handle, data->tableName, data->record.key);

    _worker_request_mem(&hints, MEMP_REQ_SELECT, payload, payloadSize, _req);

    if (!_scripted) _worker_parse_result(_req);
}

void worker_handle_insert(task_t* _req, bool _scripted)
{
    data_insert_t* data = _req->data;

    mempool_hints_t hints;
    CX_MEM_ZERO(hints);
    hints.query = QUERY_INSERT;
    hints.tableName = data->tableName;
    hints.key = data->record.key;

    payload_t payload;
    uint32_t payloadSize = mem_pack_req_insert(payload, sizeof(payload),
        _req->handle, data->tableName, data->record.key, data->record.value, data->record.timestamp);

    _worker_request_mem(&hints, MEMP_REQ_INSERT, payload, payloadSize, _req);

    if (!_scripted) _worker_parse_result(_req);
}

void worker_handle_journal(task_t* _req, bool _scripted)
{
    mempool_journal(&_req->err);

    if (!_scripted) _worker_parse_result(_req);
}

void worker_handle_addmem(task_t* _req, bool _scripted)
{
    data_addmem_t* data = _req->data;

    mempool_assign(data->memNumber, data->consistency, &_req->err);

    if (!_scripted) _worker_parse_result(_req);
}

void worker_handle_run(task_t* _req)
{
    data_run_t* data = _req->data;

    bool initialized = true;

    if (0 == data->lineNumber)
    {
        data->script = cx_linesf_open(data->scriptFilePath, CX_LINESF_OPEN_READ, &_req->err);
        if (NULL != data->script)
        {
            if (cx_file_touch(&data->outputFilePath, NULL))
                data->output = fopen(data->outputFilePath, "w");
            
            if (NULL == data->output)
            {
                initialized = false;
                CX_ERR_SET(&_req->err, ERR_GENERIC, "Log file creation failed '%s'.", data->outputFilePath);
            }
        }
        else
        {
            initialized = false;
        }

        if (initialized) data->lineNumber = 1;
    }

    if (initialized)
    {
        bool eof = false;
        uint16_t quantumCount = 0;
        char buff[4096 + 1];
        cx_cli_cmd_t cmd;
        CX_MEM_ZERO(cmd);

        while (quantumCount < g_ctx.cfg.quantum)
        {
            eof = (0 == cx_linesf_line_read(data->script, data->lineNumber++, buff, sizeof(buff)));
            if (eof) break;

            cx_cli_cmd_destroy(&cmd);
            cx_cli_cmd_parse(buff, &cmd);
            if (NULL != cmd.header && strlen(cmd.header) > 0)
            {
                report_query(buff, data->output);

                if (!_worker_run_query_scripted(&cmd, _req))
                    break;

                quantumCount++;
            }
        }
        cx_cli_cmd_destroy(&cmd);

        if (quantumCount == g_ctx.cfg.quantum)
            CX_ERR_SET(&_req->err, ERR_QUANTUM_EXHAUSTED, "Quantum exhausted.");
    }

    _worker_parse_result(_req);
}

/****************************************************************************************
***  PRIVATE FUNCTIONS
***************************************************************************************/

static void _worker_parse_result(task_t* _req)
{
    if (ERR_QUANTUM_EXHAUSTED == _req->err.code)
    {
        _req->state = TASK_STATE_BLOCKED_RESCHEDULE;
    }
    else
    {
        taskman_completion(_req);
    }
}

static bool _worker_request_mem(mempool_hints_t* _hints, uint8_t _header, const char* _payload, uint32_t _payloadSize, task_t* _task)
{
    int32_t result = 0;
    CX_ERR_CLEAR(&_task->err);

    pthread_mutex_lock(&_task->responseMtx);
    _task->state = TASK_STATE_RUNNING_AWAITING;
    pthread_mutex_unlock(&_task->responseMtx);

    do
    {
        _task->responseMemNumber = mempool_get(_hints, &_task->err);
        if (_task->responseMemNumber == INVALID_MEM_NUMBER)
        {
            // node allocation failed, there might not be any mem node satisfying this
            // criteria (err contains the actual reason of the failure)
            break;
        }

        result = mempool_node_req(_task->responseMemNumber, _header, _payload, _payloadSize);

        if (CX_NET_SEND_BUFFER_FULL == result)
            mempool_node_wait(_task->responseMemNumber);

    } while (result != CX_NET_SEND_OK);

    if (CX_NET_SEND_OK == result)
    {
        // wait for the response
        pthread_mutex_lock(&_task->responseMtx);
        {
            while (TASK_STATE_RUNNING_AWAITING == _task->state)
            {
                pthread_cond_wait(&_task->responseCond, &_task->responseMtx);
            }
        }
        pthread_mutex_unlock(&_task->responseMtx);
    }
    else
    {
        pthread_mutex_lock(&_task->responseMtx);
        _task->state = TASK_STATE_RUNNING;
        pthread_mutex_unlock(&_task->responseMtx);
    }

    return (ERR_NONE == _task->err.code);
}

static bool _worker_run_query_scripted(cx_cli_cmd_t* _cmd, task_t* _task)
{
    FILE*       output = ((data_run_t*)_task->data)->output;
    void*       originalData = _task->data;
    TASK_TYPE   originalType = _task->type;
    bool        validCommand = false;
    char*       tableName = NULL;
    uint16_t    key = 0;
    char*       value = NULL;
    uint64_t    timestamp = 0;
    uint8_t     consistency = 0;
    uint16_t    numPartitions = 0;
    uint32_t    compactionInterval = 0;
    uint16_t    memNumber = 0;

    if (strcmp("CREATE", _cmd->header) == 0)
    {
        if (cli_parse_create(_cmd, &_task->err, &tableName, &consistency, &numPartitions, &compactionInterval))
        {
            validCommand = true;

            data_create_t* data = CX_MEM_STRUCT_ALLOC(data);
            cx_str_copy(data->tableName, sizeof(data->tableName), tableName);
            data->consistency = consistency;
            data->numPartitions = numPartitions;
            data->compactionInterval = compactionInterval;

            _task->type = TASK_WT_CREATE;
            _task->data = data;

            worker_handle_create(_task, true);
            report_create(_task, output);
        }
    }
    else if (strcmp("DROP", _cmd->header) == 0)
    {
        if (cli_parse_drop(_cmd, &_task->err, &tableName))
        {
            validCommand = true;

            data_drop_t* data = CX_MEM_STRUCT_ALLOC(data);
            cx_str_copy(data->tableName, sizeof(data->tableName), tableName);

            _task->type = TASK_WT_DROP;
            _task->data = data;

            worker_handle_drop(_task, true);
            report_drop(_task, output);
        }
    }
    else if (strcmp("DESCRIBE", _cmd->header) == 0)
    {
        if (cli_parse_describe(_cmd, &_task->err, &tableName))
        {
            validCommand = true;

            data_describe_t* data = CX_MEM_STRUCT_ALLOC(data);

            if (NULL != tableName) // specific table describe
            {
                data->tablesCount = 1;
                data->tables = CX_MEM_ARR_ALLOC(data->tables, data->tablesCount);
                cx_str_copy(data->tables[0].name, sizeof(data->tables[0].name), tableName);
            }

            _task->type = TASK_WT_DESCRIBE;
            _task->data = data;

            worker_handle_describe(_task, true);
            report_describe(_task, output);
        }
    }
    else if (strcmp("SELECT", _cmd->header) == 0)
    {
        if (cli_parse_select(_cmd, &_task->err, &tableName, &key))
        {
            validCommand = true;

            data_select_t* data = CX_MEM_STRUCT_ALLOC(data);
            cx_str_copy(data->tableName, sizeof(data->tableName), tableName);
            data->record.key = key;

            _task->type = TASK_WT_SELECT;
            _task->data = data;

            worker_handle_select(_task, true);
            report_select(_task, output);
        }
    }
    else if (strcmp("INSERT", _cmd->header) == 0)
    {
        if (cli_parse_insert(_cmd, &_task->err, &tableName, &key, &value, &timestamp))
        {
            validCommand = true;

            data_select_t* data = CX_MEM_STRUCT_ALLOC(data);
            cx_str_copy(data->tableName, sizeof(data->tableName), tableName);
            data->record.key = key;
            data->record.value = cx_str_copy_d(value);
            data->record.timestamp = timestamp;

            _task->type = TASK_WT_INSERT;
            _task->data = data;

            worker_handle_insert(_task, true);
            report_insert(_task, output);
        }
    }
    else if (strcmp("JOURNAL", _cmd->header) == 0)
    {
        validCommand = true;

        _task->type = TASK_WT_JOURNAL;
        _task->data = NULL;

        worker_handle_journal(_task, true);
        report_journal(_task, output);
    }
    else if (strcmp("ADD", _cmd->header) == 0)
    {
        if (cli_parse_add_memory(_cmd, &_task->err, &memNumber, &consistency))
        {
            validCommand = true;

            data_addmem_t* data = CX_MEM_STRUCT_ALLOC(data);
            data->memNumber = memNumber;
            data->consistency = consistency;

            _task->type = TASK_WT_ADDMEM;
            _task->data = data;

            worker_handle_addmem(_task, true);
            report_addmem(_task, output);
        }
    }
    else
    {
        CX_ERR_SET(&_task->err, 1, "Unknown command '%s'.", _cmd->header);
    }

    if (validCommand)
    {
        task_data_free(_task->type, _task->data);
    }

    // restore original data
    _task->data = originalData;
    _task->type = originalType;

#ifdef DELAYS_ENABLED
    sleep(g_ctx.cfg.delayRun);
#endif

    return (ERR_NONE == _task->err.code);
}