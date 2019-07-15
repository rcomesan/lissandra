#include "lfs_worker.h"
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
    table_t* table = NULL;

    fs_table_create(&table,
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

    fs_table_delete(data->tableName, &table, &_req->err);

    _worker_parse_result(_req, table);
}

void worker_handle_describe(task_t* _req)
{
    data_describe_t* data = _req->data;

    if (1 == data->tablesCount && NULL != data->tables)
    {
        fs_table_describe(data->tables[0].name, &data->tables[0], &_req->err);
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

    if (fs_table_avail_guard_begin(data->tableName, &_req->err, &table))
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

    _worker_parse_result(_req, table);
}

void worker_handle_insert(task_t* _req)
{
    data_insert_t* data = _req->data;
    table_t* table = NULL;

    if (fs_table_avail_guard_begin(data->tableName, &_req->err, &table))
    {
        if (0 == data->record.timestamp)
            data->record.timestamp = cx_time_epoch_ms();

        memtable_add(&table->memtable, &data->record, 1);

        fs_table_avail_guard_end(table);
    }

    _worker_parse_result(_req, table);
}

void worker_handle_dump(task_t* _req)
{
    data_insert_t* data = _req->data;
    table_t* table = NULL;

    if (fs_table_avail_guard_begin(data->tableName, &_req->err, &table))
    {
        memtable_make_dump(&table->memtable, &_req->err);

        fs_table_avail_guard_end(table);
    }

    _worker_parse_result(_req, table);
}

void worker_handle_compact(task_t* _req)
{
    bool                success = true;
    table_t*            table = _req->table;
    data_compact_t*     data = _req->data;
    cx_file_explorer_t* exp = NULL;
    uint16_t*           dumpNumbers = NULL;
    memtable_t          dumpsMemt;
    bool                dumpsMemtInitialized = false;
    memtable_t          tempMemt;
    fs_file_t           dumpFile;

    // note: pointer to the table being compacted by this task is guaranteed to be valid always since
    // table deallocation (on drop request) only proceeds if compaction is not being performed.
    
    /////////////////////////////////////////////////////////////////////////////////////
    // [STAGE #1] define the scope of our compaction renaming D#.tmp to D#.tmpc files 
    if (success && fs_table_block(table))
    {
        exp = fs_table_explorer(table->meta.name, &_req->err);
        if (NULL != exp)
        {
            uint16_t   dumpNumberMax = fs_table_dump_number_next(table->meta.name) - 1;
            cx_path_t  dumpPath;
            dumpNumbers = CX_MEM_ARR_ALLOC(dumpNumbers, dumpNumberMax);

            while (cx_file_explorer_next_file(exp, &dumpPath))
            {
                if (fs_is_dump(&dumpPath, &dumpNumbers[data->dumpsCount], NULL))
                    data->dumpsCount++;
            }

            for (uint32_t i = 0; i < data->dumpsCount; i++)
            {
                if (fs_table_dump_get(table->meta.name, dumpNumbers[i], false, &dumpFile, NULL))
                {
                    cx_file_set_extension(&dumpFile.path, LFS_DUMP_EXTENSION_COMPACTION, &dumpPath);

                    if (!cx_file_move(&dumpFile.path, &dumpPath, &_req->err))
                    {
                        success = false;
                        break;
                    }
                }
            }
        }

        fs_table_unblock(table);
        data->beginStageTime = cx_reslock_blocked_time(&table->reslock);
    }

    /////////////////////////////////////////////////////////////////////////////////////
    // [STAGE #2] perform the compaction merging P*.bin & D#.tmpc.
    // this will create a new new set of P*.binc files ready to be used as the final 
    // a replacement in the next stage.
    if (success)
    {
        // load dumps .tmpc files into a tempMemt and merge the records into the dumpsMemt
        if (memtable_init(table->meta.name, false, &dumpsMemt, &_req->err))
        {
            dumpsMemtInitialized = true;

            for (uint32_t i = 0; i < data->dumpsCount; i++)
            {
                if (memtable_init_from_dump(table->meta.name, dumpNumbers[i], true, &tempMemt, &_req->err))
                {
                    memtable_add(&dumpsMemt, tempMemt.records, tempMemt.recordsCount);
                    memtable_clear(&tempMemt);
                    memtable_destroy(&tempMemt);
                }
            }

            // sort the entries recovered from dumps and remove duplicates.
            memtable_preprocess(&dumpsMemt);

            // make the new partitions
            uint32_t dumpsEntries = 0;
            uint32_t dumpsPos = 0;
            for (uint16_t i = 0; i < table->meta.partitionsCount; i++)
            {
                // figure our how many records exist in our dumps memtable that fit in the current partition number.
                dumpsEntries = 0;
                while ((dumpsPos + dumpsEntries) < dumpsMemt.recordsCount 
                    && i == (dumpsMemt.records[dumpsPos + dumpsEntries].key % table->meta.partitionsCount))
                {
                    dumpsEntries++;
                }

                if (dumpsEntries > 0)
                {
                    // initialize the new partition, add records, preprocess and save it to a new temporary .binc partition file.
                    if (memtable_init_from_part(table->meta.name, i, false, &tempMemt, &_req->err))
                    {
                        memtable_add(&tempMemt, &dumpsMemt.records[dumpsPos], dumpsEntries);
                        memtable_preprocess(&tempMemt);

                        // save the new partition (make_part will serialize the memtable to a .binc temporary partition file).
                        success = memtable_make_part(&tempMemt, i, &_req->err);
                        memtable_destroy(&tempMemt);

                        if (!success) break;
                    }

                    dumpsPos += dumpsEntries;
                }
            }
        }
        else
        {
            success = false;
        }
    }

    /////////////////////////////////////////////////////////////////////////////////////
    // [STAGE #3] rename P#.binc -> P#.bin replacing the old partitions with the new ones
    if (success && fs_table_block(table))
    {
        // delete the dump files being compacted (those previously renamed to .tmpc)
        for (uint32_t i = 0; i < data->dumpsCount; i++)
        {
            if (!fs_table_dump_delete(table->meta.name, dumpNumbers[i], true, &_req->err))
            {
                CX_WARN(CX_ALW, "dumpc file deletion failed! %s", _req->err);
            }
        }

        // delete the current partitions and replace them with the new ones
        fs_file_t oldPartFile;
        cx_path_t tmpPath;
        for (uint16_t i = 0; i < table->meta.partitionsCount; i++)
        {
            if (fs_table_part_get(table->meta.name, i, false, &oldPartFile, &_req->err))
            {
                cx_file_set_extension(&oldPartFile.path, LFS_PART_EXTENSION_COMPACTION, &tmpPath);

                if (cx_file_exists(&tmpPath))
                {
                    if (!fs_file_delete(&oldPartFile, &_req->err)) break;
                    if (!cx_file_move(&tmpPath, &oldPartFile.path, &_req->err)) break;
                }
            }
        }    

        fs_table_unblock(table);
        data->endStageTime = cx_reslock_blocked_time(&table->reslock);
    }

    if (NULL != exp) cx_file_explorer_destroy(exp);
    if (NULL != dumpNumbers) free(dumpNumbers);
    if (dumpsMemtInitialized)
    {
        memtable_clear(&dumpsMemt);
        memtable_destroy(&dumpsMemt);
    }

    _worker_parse_result(_req, table);
}

/****************************************************************************************
***  PRIVATE FUNCTIONS
***************************************************************************************/

static void _worker_parse_result(task_t* _req, table_t* _affectedTable)
{
    _req->table = _affectedTable;

    if (ERR_TABLE_BLOCKED == _req->err.code)
    {
        _req->state = TASK_STATE_BLOCKED_RESCHEDULE;
    }
    else
    {
        taskman_completion(_req);
    }

#ifdef DELAYS_ENABLED
    sleep(g_ctx.cfg.delay);
#endif

}