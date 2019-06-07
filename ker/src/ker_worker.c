#include "ker.h"
#include "ker_worker.h"
#include "mempool.h"

#include <ker/defines.h>
#include <mem/mem_protocol.h>

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/str.h>
#include <cx/file.h>
#include <cx/timer.h>

#include <unistd.h>

/****************************************************************************************
***  PRIVATE DECLARATIONS
***************************************************************************************/

static void         _worker_parse_result(task_t* _req);

static bool         _worker_request_mem(mempool_hints_t* _hints, uint8_t _header, const char* _payload, uint32_t _payloadSize, task_t* _task);

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

void worker_handle_addmem(task_t* _req, bool _scripted)
{
    data_addmem_t* data = _req->data;

    mempool_assign(data->memNumber, data->consistency, &_req->err);

    if (!_scripted) _worker_parse_result(_req);
}

void worker_handle_run(task_t* _req)
{
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