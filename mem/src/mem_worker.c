#include "mem.h"
#include "mem_worker.h"
#include <lfs/lfs_protocol.h>

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/str.h>
#include <cx/file.h>
#include <cx/timer.h>

#include <ker/defines.h>
#include <unistd.h>

/****************************************************************************************
***  PRIVATE DECLARATIONS
***************************************************************************************/

static void         _worker_parse_result(task_t* _req);

static bool         _worker_wait_response(task_t* _req);

/****************************************************************************************
***  PUBLIC FUNCTIONS
***************************************************************************************/

void worker_handle_create(task_t* _req)
{
    data_create_t* data = _req->data;

    payload_t payload;
     uint32_t payloadSize = lfs_pack_req_create(payload, sizeof(payload),
        _req->handle, data->tableName, data->consistency,
        data->numPartitions, data->compactionInterval);

    _req->state = TASK_STATE_RUNNING_AWAITING;
    cx_net_send(g_ctx.lfs, LFSP_REQ_CREATE, payload, payloadSize, INVALID_HANDLE);

    _worker_wait_response(_req);

    _worker_parse_result(_req);
}

void worker_handle_drop(task_t* _req)
{
    data_drop_t* data = _req->data;

    payload_t payload;
    uint32_t payloadSize = lfs_pack_req_drop(payload, sizeof(payload),
        _req->handle, data->tableName);

    _req->state = TASK_STATE_RUNNING_AWAITING;
    cx_net_send(g_ctx.lfs, LFSP_REQ_DROP, payload, payloadSize, INVALID_HANDLE);

    _worker_wait_response(_req);

    _worker_parse_result(_req);
}

void worker_handle_describe(task_t* _req)
{
    data_describe_t* data = _req->data;

    payload_t payload;
    uint32_t payloadSize = lfs_pack_req_describe(payload, sizeof(payload),
        _req->handle, data->tablesCount == 1 
        ? data->tables[0].name
        : NULL);

    _req->state = TASK_STATE_RUNNING_AWAITING;
    cx_net_send(g_ctx.lfs, LFSP_REQ_DESCRIBE, payload, payloadSize, INVALID_HANDLE);

    _worker_wait_response(_req);

    _worker_parse_result(_req);
}

void worker_handle_select(task_t* _req)
{
    data_select_t* data = _req->data;

    payload_t payload;
    uint32_t payloadSize = lfs_pack_req_select(payload, sizeof(payload),
        _req->handle, data->tableName, data->record.key);

    _req->state = TASK_STATE_RUNNING_AWAITING;
    cx_net_send(g_ctx.lfs, LFSP_REQ_SELECT, payload, payloadSize, INVALID_HANDLE);

    _worker_wait_response(_req);

    _worker_parse_result(_req);
}

void worker_handle_insert(task_t* _req)
{
    data_insert_t* data = _req->data;
    _worker_parse_result(_req);
}

/****************************************************************************************
***  PRIVATE FUNCTIONS
***************************************************************************************/

static void _worker_parse_result(task_t* _req)
{
    if (ERR_TABLE_BLOCKED == _req->err.code)
    {
        _req->state = TASK_STATE_BLOCKED_RESCHEDULE;
    }
    else
    {
        taskman_completion(_req);
    }
}

static bool _worker_wait_response(task_t* _req)
{
    pthread_mutex_lock(&_req->responseMtx);
    {
        while (TASK_STATE_RUNNING_AWAITING == _req->state)
        {
            pthread_cond_wait(&_req->responseCond, &_req->responseMtx);
        }
    }
    pthread_mutex_unlock(&_req->responseMtx);

    return (ERR_NONE == _req->err.code);
}