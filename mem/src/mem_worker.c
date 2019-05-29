#include "mem_worker.h"

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

//static void         _worker_parse_result(task_t* _req, table_t* _dependingTable);

/****************************************************************************************
***  PUBLIC FUNCTIONS
***************************************************************************************/

void worker_handle_create(task_t* _req)
{
    data_create_t* data = _req->data;
    //_worker_parse_result(_req, table);
}

void worker_handle_drop(task_t* _req)
{
    data_drop_t* data = _req->data;
    //_worker_parse_result(_req, table);
}

void worker_handle_describe(task_t* _req)
{
    data_describe_t* data = _req->data;
    //_worker_parse_result(_req, NULL);
}

void worker_handle_select(task_t* _req)
{
    data_select_t* data = _req->data;
    //_worker_parse_result(_req, table);
}

void worker_handle_insert(task_t* _req)
{
    data_insert_t* data = _req->data;
    //_worker_parse_result(_req, table);
}

void worker_handle_dump(task_t* _req)
{
    data_insert_t* data = _req->data;
    //_worker_parse_result(_req, table);
}

void worker_handle_compact(task_t* _req)
{
    data_compact_t* data = _req->data;
    //_worker_parse_result(_req, table);
}

/****************************************************************************************
***  PRIVATE FUNCTIONS
***************************************************************************************/

//static void _worker_parse_result(task_t* _req, table_t* _affectedTable)
//{
//    if (NULL != _affectedTable)
//    {
//        _req->tableHandle = _affectedTable->handle;
//    }
//    else
//    {
//        _req->tableHandle = INVALID_HANDLE;
//    }
//
//    if (ERR_TABLE_BLOCKED == _req->err.code)
//    {
//        _req->state = TASK_STATE_BLOCKED_RESCHEDULE;
//    }
//    else
//    {
//        taskman_completion(_req);
//    }
//}