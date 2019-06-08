#include "mem.h"
#include "mem_worker.h"
#include "mm.h"

#include <ker/defines.h>
#include <lfs/lfs_protocol.h>

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/str.h>
#include <cx/file.h>
#include <cx/timer.h>

#include <unistd.h>

/****************************************************************************************
***  PRIVATE DECLARATIONS
***************************************************************************************/

static void         _worker_parse_result(task_t* _req, segment_t* _affectedTable);

static bool         _worker_request_lfs(uint8_t _header, const char* _payload, uint32_t _payloadSize, task_t* _task);

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

    _worker_request_lfs(LFSP_REQ_CREATE, payload, payloadSize, _req);
    
    _worker_parse_result(_req, NULL);
}

void worker_handle_drop(task_t* _req)
{
    data_drop_t* data = _req->data;
    segment_t* table = NULL;

    payload_t payload;
    uint32_t payloadSize = lfs_pack_req_drop(payload, sizeof(payload),
        _req->handle, data->tableName);

    _worker_request_lfs(LFSP_REQ_DROP, payload, payloadSize, _req);

    if (mm_segment_delete(data->tableName, &table, NULL))
    {
        // block the resource. it shouldn't be accessible at this point since
        // the segment is already removed from the table... but anyway
        cx_reslock_block(&table->reslock);

        // insert a MT_TASK to free this table in a thread-safe way.
        data_free_t* data = CX_MEM_STRUCT_ALLOC(data);
        data->resourceType = RESOURCE_TYPE_TABLE;
        data->resourcePtr = table;

        task_t* task = taskman_create(TASK_ORIGIN_INTERNAL, TASK_MT_FREE, data, NULL);
        if (NULL != task)
        {
            task->state = TASK_STATE_NEW;
        }
        else
        {
            CX_ERR_SET(&_req->err, ERR_GENERIC, "We ran out of task handles. Try again later.");
        }
    }

    _worker_parse_result(_req, table);
}

void worker_handle_describe(task_t* _req)
{
    data_describe_t* data = _req->data;

    payload_t payload;
    uint32_t payloadSize = lfs_pack_req_describe(payload, sizeof(payload),
        _req->handle, data->tablesCount == 1 
        ? data->tables[0].name
        : NULL);

    _worker_request_lfs(LFSP_REQ_DESCRIBE, payload, payloadSize, _req);
    
    _worker_parse_result(_req, NULL);
}

void worker_handle_select(task_t* _req)
{
    data_select_t* data = _req->data;
    segment_t* table = NULL;

    if (mm_avail_guard_begin(&_req->err))
    { 
        if (mm_segment_avail_guard_begin(data->tableName, &_req->err, &table))
        {
            if (!mm_page_read(table, data->record.key, &data->record, &_req->err))
            {
                // record does not exist in our cache, we need to request it to the LFS
                payload_t payload;
                uint32_t payloadSize = lfs_pack_req_select(payload, sizeof(payload),
                    _req->handle, data->tableName, data->record.key);

                if (_worker_request_lfs(LFSP_REQ_SELECT, payload, payloadSize, _req))
                {
                    // result is ready! try to write it to our cache now.
                    mm_page_write(table, &data->record, false, &_req->err);
                }
            }

            mm_segment_avail_guard_end(table);
        }
        mm_avail_guard_end();
    }

    _worker_parse_result(_req, table);
}

void worker_handle_insert(task_t* _req)
{
    data_insert_t* data = _req->data;
    segment_t* table = NULL;

    if (mm_avail_guard_begin(&_req->err))
    {
        if (mm_segment_avail_guard_begin(data->tableName, &_req->err, &table))
        {
            if (0 == data->record.timestamp)
                data->record.timestamp = cx_time_epoch();

            mm_page_write(table, &data->record, true, &_req->err);
            mm_segment_avail_guard_end(table);
        }
        mm_avail_guard_end();
    }

    _worker_parse_result(_req, table);
}

void worker_handle_journal(task_t* _req)
{
    mm_journal_run(_req);

    _worker_parse_result(_req, NULL);
}

/****************************************************************************************
***  PRIVATE FUNCTIONS
***************************************************************************************/

static void _worker_parse_result(task_t* _req, segment_t* _affectedTable)
{
    _req->table = _affectedTable;

    if (ERR_MEMORY_FULL == _req->err.code
        || ERR_MEMORY_BLOCKED == _req->err.code
        || ERR_TABLE_BLOCKED == _req->err.code)
    {
        _req->state = TASK_STATE_BLOCKED_RESCHEDULE;
    }
    else
    {
        taskman_completion(_req);
    }
}

static bool _worker_request_lfs(uint8_t _header, const char* _payload, uint32_t _payloadSize, task_t* _task)
{
    int32_t result = 0;
    CX_ERR_CLEAR(&_task->err);
    
    pthread_mutex_lock(&_task->responseMtx);
    _task->state = TASK_STATE_RUNNING_AWAITING;
    pthread_mutex_unlock(&_task->responseMtx);

#ifdef DELAYS_ENABLED
    sleep(g_ctx.cfg.delayLfs);
#endif

    do
    {
        result = cx_net_send(g_ctx.lfs, _header, _payload, _payloadSize, INVALID_HANDLE);

        if (CX_NET_SEND_DISCONNECTED == result)
        {
            break;
        }
        else if (CX_NET_SEND_BUFFER_FULL == result)
        {
            cx_net_wait_outboundbuff(g_ctx.lfs, INVALID_HANDLE, -1);
        }

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

        CX_ERR_SET(&_task->err, ERR_NET_LFS_UNAVAILABLE, "LFS node is unavailable.");
    }

    return (ERR_NONE == _task->err.code);

}