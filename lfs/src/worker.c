#include "worker.h"

#include <cx/mem.h>

#include <ker/defines.h>
#include <unistd.h>

/****************************************************************************************
***  PRIVATE DECLARATIONS
***************************************************************************************/

static void         _request_main_thread_sync();

static void         _worker_parse_result(request_t* _req);

/****************************************************************************************
***  PUBLIC FUNCTIONS
***************************************************************************************/

void worker_handle_create(request_t* _req)
{
    data_create_t* data = _req->data;

    fs_table_create(data->name, 
        data->consistency, 
        data->numPartitions, 
        data->compactionInterval, 
        &data->c.err);
    
    _worker_parse_result(_req);
}

void worker_handle_drop(request_t* _req)
{
    data_drop_t* data = _req->data;

    fs_table_delete(data->name,
        &data->c.err);

    _worker_parse_result(_req);
}

void worker_handle_describe(request_t* _req)
{
    data_describe_t* data = _req->data;

    if (1 == data->tablesCount && NULL != data->tables)
    {
        table_t* table = NULL;
        if (cx_cdict_get(g_ctx.tables, data->tables[0].name, (void**)&table) && !table->deleted)
        {
            memcpy(&data->tables[0], &(table->meta), sizeof(data->tables[0]));
        }
        else
        {
            CX_ERROR_SET(&data->c.err, 1, "Table '%s' does not exist.", data->tables[0].name);
        }
    }
    else
    {
        data->tables = fs_describe(&data->tablesCount, &data->c.err);
    }

    _worker_parse_result(_req);
}

void worker_handle_select(request_t* _req)
{
    data_select_t* data = _req->data;
    sleep(1);
    data->c.err.code = ERR_NONE;
    _req->state = REQ_STATE_COMPLETED;
}

void worker_handle_insert(request_t* _req)
{
    data_insert_t* data = _req->data;
    sleep(1);
    data->c.err.code = ERR_NONE;
    _req->state = REQ_STATE_COMPLETED;
}

/****************************************************************************************
***  PRIVATE FUNCTIONS
***************************************************************************************/

static void _request_main_thread_sync()
{
    g_ctx.pendingSync = true;
}

static void _worker_parse_result(request_t* _req)
{
    if (LFS_ERR_TABLE_BLOCKED == ((data_common_t*)_req->data)->err.code)
    {
        _req->state = REQ_STATE_BLOCKED_DOAGAIN;
        _request_main_thread_sync();
    }
    else
    {
        _req->state = REQ_STATE_COMPLETED;
    }
}