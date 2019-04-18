#include "worker.h"
#include <ker/defines.h>

/****************************************************************************************
***  PUBLIC FUNCTIONS
***************************************************************************************/

void worker_handle_create(request_t* _req)
{
    data_create_t* data = _req->data;
    sleep(1);
    data->c.success = true;
    _req->state = REQ_STATE_COMPLETED;
}

void worker_handle_drop(request_t* _req)
{
    data_drop_t* data = _req->data;
    sleep(1);
    data->c.success = true;
    _req->state = REQ_STATE_COMPLETED;
}

void worker_handle_describe(request_t* _req)
{
    data_describe_t* data = _req->data;
    sleep(1);
    data->c.success = true;
    _req->state = REQ_STATE_COMPLETED;
}

void worker_handle_select(request_t* _req)
{
    data_select_t* data = _req->data;
    sleep(1);
    data->c.success = true;
    _req->state = REQ_STATE_COMPLETED;
}

void worker_handle_insert(request_t* _req)
{
    data_insert_t* data = _req->data;
    sleep(1);
    data->c.success = true;
    _req->state = REQ_STATE_COMPLETED;
}
