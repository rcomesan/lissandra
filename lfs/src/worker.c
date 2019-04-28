#include "worker.h"
#include "memtable.h"
#include "fs.h"

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/str.h>
#include <cx/fs.h>

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

    fs_table_create(&(g_ctx.tables[data->tableHandle]),
        data->name, 
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
        if (fs_table_exists(data->tables[0].name, &table))
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

    if (!fs_table_blocked_guard(data->name, &data->c.err, NULL))
    {
        table_t* table = NULL;
        if (fs_table_exists(data->name, &table))
        {
            memtable_t memt;
            cx_error_t err;
            table_record_t* rec = &data->record;
            table_record_t  recTmp;

            rec->timestamp = 0;
            rec->value = NULL;

            // search it in the corresponding partition
            uint16_t partNumber = rec->key % table->meta.partitionsCount;
            if (memtable_init_from_part(data->name, partNumber, &memt, &err))
            {
                if (memtable_find(&memt, rec->key, &recTmp)
                    && recTmp.timestamp >= rec->timestamp)
                {
                    rec->timestamp = recTmp.timestamp;
                    rec->value = cx_str_copy_d(recTmp.value);
                }

                memtable_destroy(&memt);
            }

            // search it in all the existent dumps
            uint16_t  dumpNumber = 0;
            bool      dumpDuringCompaction = false;
            cx_path_t filePath;
            cx_fs_explorer_t* exp = fs_table_explorer(data->name, &err);
            if (NULL != exp)
            {
                while (cx_fs_explorer_next_file(exp, &filePath))
                {
                    if (fs_is_dump(&filePath, &dumpNumber, &dumpDuringCompaction))
                    {
                        if (memtable_init_from_dump(data->name, dumpNumber, dumpDuringCompaction, &memt, &err))
                        {
                            if (memtable_find(&memt, rec->key, &recTmp)
                                && recTmp.timestamp >= rec->timestamp)
                            {
                                rec->timestamp = recTmp.timestamp;

                                if (NULL != rec->value) free(rec->value);
                                rec->value = cx_str_copy_d(recTmp.value);
                            }

                            memtable_destroy(&memt);
                        }
                    }
                }
                cx_fs_explorer_destroy(exp);
            }
            
            // search it in our current memtable
            if (memtable_find(&table->memtable, rec->key, &recTmp)
                && recTmp.timestamp >= rec->timestamp)
            {
                rec->timestamp = recTmp.timestamp;

                if (NULL != rec->value) free(rec->value);
                rec->value = cx_str_copy_d(recTmp.value);
            }
            
            // check if we finally found it
            if (NULL == rec->value)
            {
                CX_ERROR_SET(&data->c.err, 1, "Key %d does not exist in table '%s'.", rec->key, data->name);
            }
        }
        else
        {
            CX_ERROR_SET(&data->c.err, 1, "Table '%s' does not exist.", data->name);
        }
    } 
    
    _worker_parse_result(_req);
}

void worker_handle_insert(request_t* _req)
{
    data_insert_t* data = _req->data;

    if (!fs_table_blocked_guard(data->name, &data->c.err, NULL))
    {
        table_t* table = NULL;
        if (fs_table_exists(data->name, &table))
        {           
            memtable_add(&table->memtable, &data->record);
        }
        else
        {
            CX_ERROR_SET(&data->c.err, 1, "Table '%s' does not exist.", data->name);
        }
    }

    _worker_parse_result(_req);
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