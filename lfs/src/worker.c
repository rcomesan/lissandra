#include "worker.h"
#include "memtable.h"
#include "fs.h"

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

static void         _worker_parse_result(task_t* _req, table_t* _dependingTable);

/****************************************************************************************
***  PUBLIC FUNCTIONS
***************************************************************************************/

void worker_handle_create(task_t* _req)
{
    data_create_t* data = _req->data;
    table_t* table = &(g_ctx.tables[_req->tableHandle]);

    fs_table_create(table,
        data->tableName, 
        data->consistency, 
        data->numPartitions, 
        data->compactionInterval, 
        &_req->err);
    
    _worker_parse_result(_req, table);
}

void worker_handle_drop(task_t* _req)
{
    data_drop_t* data = _req->data;
    table_t* table;

    if (fs_table_exists(data->tableName, &table))
    {
        if (fs_table_avail_guard_begin(table, &_req->err, NULL))
        {
            fs_table_delete(data->tableName, &table, &_req->err);
            fs_table_avail_guard_end(table);
        }
    }
    else
    {
        CX_ERR_SET(&_req->err, 1, "Table '%s' does not exist.", data->tableName);
    }

    _worker_parse_result(_req, table);
}

void worker_handle_describe(task_t* _req)
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
            CX_ERR_SET(&_req->err, 1, "Table '%s' does not exist.", data->tables[0].name);
        }
    }
    else
    {
        data->tables = fs_describe(&data->tablesCount, &_req->err);
    }

    _worker_parse_result(_req, NULL);
}

void worker_handle_select(task_t* _req)
{
    data_select_t* data = _req->data;
    table_t* table = NULL;

    if (fs_table_exists(data->tableName, &table))
    {
        if (fs_table_avail_guard_begin(table, &_req->err, NULL))
        {
            memtable_t memt;
            cx_err_t err;
            table_record_t* rec = &data->record;
            table_record_t  recTmp;

            rec->timestamp = 0;
            rec->value = NULL;

            // search it in the corresponding partition
            uint16_t partNumber = rec->key % table->meta.partitionsCount;
            if (memtable_init_from_part(data->tableName, partNumber, false, &memt, &err))
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
            cx_file_explorer_t* exp = fs_table_explorer(data->tableName, &err);
            if (NULL != exp)
            {
                while (cx_file_explorer_next_file(exp, &filePath))
                {
                    if (fs_is_dump(&filePath, &dumpNumber, &dumpDuringCompaction) 
                        &&  memtable_init_from_dump(data->tableName, dumpNumber, dumpDuringCompaction, &memt, &err))
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
                cx_file_explorer_destroy(exp);
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
                CX_ERR_SET(&_req->err, 1, "Key %d does not exist in table '%s'.", rec->key, data->tableName);
            }

            fs_table_avail_guard_end(table);
        }
    }
    else
    {
        CX_ERR_SET(&_req->err, 1, "Table '%s' does not exist.", data->tableName);
    }
    
    _worker_parse_result(_req, table);
}

void worker_handle_insert(task_t* _req)
{
    data_insert_t* data = _req->data;
    table_t* table = NULL;

    if (fs_table_exists(data->tableName, &table))
    {           
        if (fs_table_avail_guard_begin(table, &_req->err, NULL))
        {
            memtable_add(&table->memtable, &data->record, 1);

            fs_table_avail_guard_end(table);
        }
    }
    else
    {
        CX_ERR_SET(&_req->err, 1, "Table '%s' does not exist.", data->tableName);
    }

    _worker_parse_result(_req, table);
}

void worker_handle_dump(task_t* _req)
{
    data_insert_t* data = _req->data;
    table_t* table = NULL;

    if (fs_table_exists(data->tableName, &table))
    {           
        if (fs_table_avail_guard_begin(table, &_req->err, NULL))
        {
            memtable_make_dump(&table->memtable, &_req->err);

            fs_table_avail_guard_end(table);
        }
    }
    else
    {
        CX_ERR_SET(&_req->err, 1, "Table '%s' does not exist.", data->tableName);
    }

    _worker_parse_result(_req, table);
}

void worker_handle_compact(task_t* _req)
{
    bool success = true;
    data_compact_t* data = _req->data;
    table_t* table = NULL;
    cx_file_explorer_t* exp = NULL;
    uint16_t*   dumpNumbers = NULL;
    uint32_t    dumpCount = 0;
    memtable_t  dumpsMem;
    bool        dumpsMemInitialized = false;
    fs_file_t   dumpFile;
    memtable_t  tempMem;

    // if this function is invoked we can safely assume the given table is fully blocked,
    // and no other operation is being performed on it.

    if (fs_table_exists(data->tableName, &table))
    {
        // define the scope of our compaction renaming .tmp files to .tmpc
        exp = fs_table_explorer(data->tableName, &_req->err);
        if (NULL != exp)
        {
            uint16_t   dumpNumberMax = fs_table_dump_number_next(data->tableName);
            cx_path_t  dumpPath;
            dumpNumbers = CX_MEM_ARR_ALLOC(dumpNumbers, dumpNumberMax);

            while (cx_file_explorer_next_file(exp, &dumpPath))
            {
                if (fs_is_dump(&dumpPath, &dumpNumbers[dumpCount], NULL))
                    dumpCount++;
            }

            for (uint32_t i = 0; i < dumpCount; i++)
            {
                if (fs_table_dump_get(data->tableName, dumpNumbers[i], false, &dumpFile, NULL))
                {
                    cx_str_copy(dumpPath, sizeof(dumpPath), dumpFile.path);
                    cx_str_copy(&dumpPath[strlen(dumpPath) - (sizeof(LFS_DUMP_EXTENSION) - 1)], sizeof(dumpPath), LFS_DUMP_EXTENSION_COMPACTION);
                    if (!cx_file_move(&dumpFile.path, &dumpPath, &_req->err))
                        goto failed;
                }
            }
        }

        // load .tmpc files into a temporary memtable, sort the entries and remove duplicates.
        if (memtable_init(data->tableName, false, &dumpsMem, &_req->err))
        {
            dumpsMemInitialized = true;

            for (uint32_t i = 0; i < dumpCount; i++)
            {
                if (memtable_init_from_dump(data->tableName, dumpNumbers[i], true, &tempMem, &_req->err))
                {
                    memtable_add(&dumpsMem, tempMem.records, tempMem.recordsCount);
                    memtable_destroy(&tempMem);
                }

                if (!fs_table_dump_delete(data->tableName, dumpNumbers[i], true, NULL))
                    goto failed;
            }
            memtable_preprocess(&dumpsMem);
        }

        // make the new partitions
        uint32_t dumpsEntries = 0;
        uint32_t dumpsPos = 0;
        for (uint16_t i = 0; i < table->meta.partitionsCount; i++)
        {
            // figure our how many records exist in our dumps memtable that fit in the current partition number.
            dumpsEntries = 0;
            while ((dumpsPos + dumpsEntries) < dumpsMem.recordsCount && i == (dumpsMem.records[dumpsPos + dumpsEntries].key % table->meta.partitionsCount))
            {
                dumpsEntries++;
            }

            if (dumpsEntries > 0)
            {              
                // initialize the new partition, add records, preprocess and save it to a new temporary .binc partition file.
                if (memtable_init_from_part(data->tableName, i, false, &tempMem, &_req->err))
                {
                    memtable_add(&tempMem, &dumpsMem.records[dumpsPos], dumpsEntries);
                    memtable_preprocess(&tempMem);

                    // save the new one
                    success = memtable_make_part(&tempMem, i, &_req->err);
                    memtable_destroy(&tempMem);
                    
                    if (!success) goto failed;
                }

                dumpsPos += dumpsEntries;
            }
        }

        // delete the current partitions and replace them with the new ones
        fs_file_t oldPartFile;
        cx_path_t tmpPath;
        for (uint16_t i = 0; i < table->meta.partitionsCount; i++)
        {
            if (fs_table_part_get(data->tableName, i, false, &oldPartFile, &_req->err))
            {
                cx_str_copy(tmpPath, sizeof(tmpPath), oldPartFile.path);
                cx_str_copy(&tmpPath[strlen(tmpPath) - (sizeof(LFS_PART_EXTENSION) - 1)], sizeof(tmpPath), LFS_PART_EXTENSION_COMPACTION);

                if (cx_file_exists(&tmpPath))
                {
                    if (!fs_file_delete(&oldPartFile, &_req->err))
                        goto failed;

                    if (!cx_file_move(&tmpPath, &oldPartFile.path, &_req->err))
                        goto failed;
                }
            }
        }
    }
    else
    {
        CX_ERR_SET(&_req->err, 1, "Table '%s' does not exist.", data->tableName);
    }

failed:
    if (NULL != exp) cx_file_explorer_destroy(exp);
    if (NULL != dumpNumbers) free(dumpNumbers);
    if (dumpsMemInitialized) memtable_destroy(&dumpsMem);

    _worker_parse_result(_req, table);
}

/****************************************************************************************
***  PRIVATE FUNCTIONS
***************************************************************************************/

static void _worker_parse_result(task_t* _req, table_t* _affectedTable)
{
    if (NULL != _affectedTable)
    {
        _req->tableHandle = _affectedTable->handle;
    }
    else
    {
        _req->tableHandle = INVALID_HANDLE;
    }

    if (LFS_ERR_TABLE_BLOCKED == _req->err.code)
    {
        _req->state = TASK_STATE_BLOCKED_RESCHEDULE;
    }
    else
    {
        taskman_completion(_req);
    }
}