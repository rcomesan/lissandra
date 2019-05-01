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
            CX_ERR_SET(&data->c.err, 1, "Table '%s' does not exist.", data->tables[0].name);
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

    table_t* table = NULL;
    if (fs_table_exists(data->name, &table))
    {
        if (fs_table_avail_guard_begin(table, &data->c.err, NULL))
        {
            memtable_t memt;
            cx_err_t err;
            table_record_t* rec = &data->record;
            table_record_t  recTmp;

            rec->timestamp = 0;
            rec->value = NULL;

            // search it in the corresponding partition
            uint16_t partNumber = rec->key % table->meta.partitionsCount;
            if (memtable_init_from_part(data->name, partNumber, false, &memt, &err))
            {
                if (memtable_find(&memt, rec->key, &recTmp) && recTmp.timestamp >= rec->timestamp)
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
                while (cx_fs_explorer_next_file(exp, &filePath) && fs_is_dump(&filePath, &dumpNumber, &dumpDuringCompaction))
                {
                    if (memtable_init_from_dump(data->name, dumpNumber, dumpDuringCompaction, &memt, &err))
                    {
                        if (memtable_find(&memt, rec->key, &recTmp) && recTmp.timestamp >= rec->timestamp)
                        {
                            rec->timestamp = recTmp.timestamp;

                            if (NULL != rec->value) free(rec->value);
                            rec->value = cx_str_copy_d(recTmp.value);
                        }

                        memtable_destroy(&memt);
                    }
                }
                cx_fs_explorer_destroy(exp);
            }

            // search it in our current memtable
            if (memtable_find(&table->memtable, rec->key, &recTmp) && recTmp.timestamp >= rec->timestamp)
            {
                rec->timestamp = recTmp.timestamp;

                if (NULL != rec->value) free(rec->value);
                rec->value = cx_str_copy_d(recTmp.value);
            }

            // check if we finally found it
            if (NULL == rec->value)
            {
                CX_ERR_SET(&data->c.err, 1, "Key %d does not exist in table '%s'.", rec->key, data->name);
            }

            fs_table_avail_guard_end(table);
        }
    }
    else
    {
        CX_ERR_SET(&data->c.err, 1, "Table '%s' does not exist.", data->name);
    }
    
    _worker_parse_result(_req);
}

void worker_handle_insert(request_t* _req)
{
    data_insert_t* data = _req->data;

    table_t* table = NULL;
    if (fs_table_exists(data->name, &table))
    {           
        if (fs_table_avail_guard_begin(table, &data->c.err, NULL))
        {
            memtable_add(&table->memtable, &data->record, 1);

            fs_table_avail_guard_end(table);
        }
    }
    else
    {
        CX_ERR_SET(&data->c.err, 1, "Table '%s' does not exist.", data->name);
    }

    _worker_parse_result(_req);
}

void worker_handle_compact(request_t* _req)
{
    data_compact_t* data = _req->data;
    table_t* table = NULL;
    cx_fs_explorer_t* exp = NULL;
    uint16_t*   dumpNumbers = NULL;
    uint32_t    dumpCount = 0;
    memtable_t* dumpsMem = NULL;
    memtable_t* tempMem = NULL;

    if (fs_table_exists(data->name, &table))
    {
        // define the scope of our compaction renaming .tmp files to .tmpc
        exp = fs_table_explorer(data->name, &data->c.err);
        if (NULL != exp)
        {
            uint16_t   dumpNumberMax = fs_table_get_dump_number_next(data->name);
            cx_path_t  dumpPath;
            cx_path_t  dumpNewPath;
            dumpNumbers = CX_MEM_ARR_ALLOC(dumpNumbers, dumpNumberMax);

            while (cx_fs_explorer_next_file(exp, &dumpPath) && fs_is_dump(&dumpPath, &dumpNumbers[dumpCount], NULL))
            {
                dumpCount++;
            }

            for (uint32_t i = 0; i < dumpCount; i++)
            {
                cx_str_copy(dumpNewPath, sizeof(dumpNewPath), dumpPath);
                cx_str_copy(&dumpNewPath[strlen(dumpNewPath) - sizeof(LFS_DUMP_EXTENSION)], 5, LFS_DUMP_EXTENSION_COMPACTION);
                if (!cx_fs_move(&dumpPath, &dumpNewPath, &data->c.err))
                    goto failed;
            }
        }

        // load .tmpc files into a temporary memtable, sort the entries and remove duplicates.
        if (memtable_init(data->name, false, dumpsMem, &data->c.err))
        {
            for (uint32_t i = 0; i < dumpCount; i++)
            {
                if (memtable_init_from_dump(data->name, dumpNumbers[i], true, tempMem, &data->c.err))
                {
                    memtable_add(dumpsMem, tempMem->records, tempMem->recordsCount);
                    memtable_destroy(tempMem);
                    tempMem = NULL;
                }
            }
            memtable_preprocess(dumpsMem);
        }

        // make the new partitions, and free the old ones.
        uint32_t dumpsEntries = 0;
        uint32_t dumpsPos = 0;
        for (uint16_t i = 0; i < table->meta.partitionsCount; i++)
        {
            // figure our how many records exist in our dumps memtable that fit in the current partition number.
            dumpsEntries = 0;
            while (i == (dumpsMem->records[dumpsPos + dumpsEntries].key % table->meta.partitionsCount))
            {
                dumpsEntries++;
            }

            if (dumpsEntries > 0)
            {
                // initialize the partition, add records, preprocess and save to a temporary (new) .binc partition file.
                if (memtable_init_from_part(data->name, i, false, tempMem, &data->c.err))
                {
                    memtable_add(tempMem, &dumpsMem->records[dumpsPos], dumpsEntries);
                    memtable_preprocess(tempMem);

                    if (!memtable_make_part(tempMem, i, &data->c.err))
                        goto failed;

                    memtable_destroy(tempMem);
                    tempMem = NULL;
                }

                dumpsPos += dumpsEntries;
            }
        }
    }
    else
    {
        CX_ERR_SET(&data->c.err, 1, "Table '%s' does not exist.", data->name);
    }

failed:
    if (NULL != exp) cx_fs_explorer_destroy(exp);
    if (NULL != dumpNumbers) free(dumpNumbers);
    if (NULL != dumpsMem) memtable_destroy(dumpsMem);
    if (NULL != tempMem) memtable_destroy(tempMem);

    _worker_parse_result(_req);
}

/****************************************************************************************
***  PRIVATE FUNCTIONS
***************************************************************************************/

static void _worker_parse_result(request_t* _req)
{
    if (LFS_ERR_TABLE_BLOCKED == ((data_common_t*)_req->data)->err.code)
    {
        _req->state = REQ_STATE_BLOCKED_DOAGAIN;
    }
    else
    {
        _req->state = REQ_STATE_COMPLETED;
    }
}