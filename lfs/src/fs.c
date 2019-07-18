#include "fs.h"

#include "memtable.h"

#include <cx/mem.h>
#include <cx/file.h>
#include <cx/str.h>
#include <cx/math.h>
#include <cx/timer.h>
#include <ker/taskman.h>

#include <commons/config.h>

#include <string.h>
#include <math.h>

static fs_ctx_t*       m_fsCtx = NULL;        // private filesystem context

#define BIT_IS_SET(_var, _i) ((_var) & (1 << (_i)))

#define SEGMENT_BITS (sizeof(uint32_t) * CHAR_BIT)

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool         _fs_is_lfs(cx_path_t* _rootDir);

static uint32_t     _fs_calc_bitmap_size(uint32_t _maxBlocks);

static bool         _fs_bootstrap(cx_path_t* _rootDir, uint32_t _maxBlocks, uint32_t _blockSize, cx_err_t* _err);

static bool         _fs_load_meta(cx_err_t* _err);

static bool         _fs_load_tables(cx_err_t* _err);

static bool         _fs_load_blocks(cx_err_t* _err);

static bool         _fs_file_save(fs_file_t* _outFile, cx_err_t* _err);

static bool         _fs_file_load(fs_file_t* _file, cx_err_t* _err);

static void         _fs_get_dump_path(cx_path_t* _outFilePath, const char* _tableName, uint16_t _dumpNumber, bool _isDuringCompaction);

static void         _fs_get_part_path(cx_path_t* _outFilePath, const char* _tableName, uint16_t _partNumber, bool _isDuringCompaction);

static void         _fs_get_block_path(cx_path_t* _outFilePath, uint32_t _blockNumber);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool fs_init(const char* _rootDir, uint32_t _blocksCount, uint32_t _blocksSize, cx_err_t* _err)
{
    CX_CHECK(NULL == m_fsCtx, "fs is already initialized!");

    m_fsCtx = CX_MEM_STRUCT_ALLOC(m_fsCtx);
    CX_ERR_CLEAR(_err);

    bool rootDirOk = false;
    cx_path_t rootDir;
    cx_file_path(&rootDir, _rootDir);

    if (cx_file_exists(&rootDir))
    {
        if (cx_file_is_folder(&rootDir))
        {
            rootDirOk = _fs_is_lfs(&rootDir);
            if (!rootDirOk)
            {
                CX_ERR_SET(_err, ERR_INIT_FS_ROOTDIR,
                    "the given root dir '%s' exists but it's not a lissandra file system (%s file is missing).",
                    rootDir, LFS_ROOT_FILE_MARKER);
            }
        }
        else
        {
            rootDirOk = false;
            CX_ERR_SET(_err, ERR_INIT_FS_ROOTDIR, "the given root dir '%s' already exists and is a file!", rootDir);
        }
    }
    else
    {
        rootDirOk = _fs_bootstrap(&rootDir, _blocksCount, _blocksSize, _err);
    }

    if (rootDirOk)
    {
        cx_str_copy(m_fsCtx->rootDir, sizeof(m_fsCtx->rootDir), rootDir);
        CX_INFO("filesystem mount point: %s", m_fsCtx->rootDir);
        
        m_fsCtx->mtxBlocksInit = (0 == pthread_mutex_init(&m_fsCtx->mtxBlocks, NULL));

        if (!m_fsCtx->mtxBlocksInit)
        {
            CX_ERR_SET(_err, 1, "pthread mutex initialization failed!");
        }

        return true
            && m_fsCtx->mtxBlocksInit
            && _fs_load_meta(_err)
            && _fs_load_tables(_err)
            && _fs_load_blocks(_err);
    }

    return false;
}

void fs_destroy()
{
    if (NULL == m_fsCtx) return;

    // destroy blocksMap
    free(m_fsCtx->blocksMap);
    m_fsCtx->blocksMap = NULL;
    
    // close bitmap file
    fflush(m_fsCtx->blocksFile);
    fclose(m_fsCtx->blocksFile);
    m_fsCtx->blocksFile = NULL;

    // destroy tablesMap
    cx_cdict_destroy(m_fsCtx->tablesMap, (cx_destroyer_cb)fs_table_destroy);

    // mutexes
    if (m_fsCtx->mtxBlocksInit)
    {
        pthread_mutex_destroy(&m_fsCtx->mtxBlocks);
        m_fsCtx->mtxBlocksInit = false;
    }

    free(m_fsCtx);
}

table_meta_t* fs_describe(uint16_t* _outTablesCount, cx_err_t* _err)
{
    table_meta_t* tables = NULL;
    char* key = NULL;
    table_t* table;
    uint16_t i = 0;

    CX_ERR_CLEAR(_err);

    pthread_mutex_lock(&m_fsCtx->tablesMap->mtx);
    cx_cdict_iter_begin(m_fsCtx->tablesMap);
    (*_outTablesCount) = (uint16_t)cx_cdict_size(m_fsCtx->tablesMap);
    if (0 < (*_outTablesCount))
    {
        tables = CX_MEM_ARR_ALLOC(tables, (*_outTablesCount));

        while (cx_cdict_iter_next(m_fsCtx->tablesMap, &key, (void**)&table))
        {
            memcpy(&(tables[i++]), &(table->meta), sizeof(tables[0]));
        }
    }
    cx_cdict_iter_end(m_fsCtx->tablesMap);
    pthread_mutex_unlock(&m_fsCtx->tablesMap->mtx);

    if ((*_outTablesCount) != i)
    {
        (*_outTablesCount) = i;

        if (i > 0)
        {
            tables = CX_MEM_ARR_REALLOC(tables, (*_outTablesCount));
        }
        else
        {
            free(tables);
            tables = NULL;
        }
    }

    return tables;
}

bool fs_table_avail_guard_begin(const char* _tableName, cx_err_t* _err, table_t** _outTable)
{
    bool available = false;

    pthread_mutex_lock(&m_fsCtx->tablesMap->mtx);
    if (fs_table_exists(_tableName, _outTable))
    {
        available = cx_reslock_avail_guard_begin(&(*_outTable)->reslock);
        if (!available)
            CX_ERR_SET(_err, ERR_TABLE_BLOCKED, "Operation cannot be performed at this time since the table is blocked. Try agian later.");
    }
    else
    {
        CX_ERR_SET(_err, ERR_GENERIC, "Table '%s' does not exist.", _tableName);
    }
    pthread_mutex_unlock(&m_fsCtx->tablesMap->mtx);

    return available;
}

void fs_table_avail_guard_end(table_t* _table)
{
    cx_reslock_avail_guard_end(&_table->reslock);
}

bool fs_table_exists(const char* _tableName, table_t** _outTable)
{
    table_t* table;

    if (cx_cdict_get(m_fsCtx->tablesMap, _tableName, (void**)&table))
    {
        if (NULL != _outTable)
        {
            (*_outTable) = table;
        }
        return true;
    }
    return false;
}

void fs_table_describe(const char* _tableName, table_meta_t* _outTableMeta, cx_err_t* _err)
{
    table_t* table = NULL;

    pthread_mutex_lock(&m_fsCtx->tablesMap->mtx);
    if (fs_table_exists(_tableName, &table))
    {
        memcpy(_outTableMeta, &(table->meta), sizeof(*_outTableMeta));
    }
    else
    {
        CX_ERR_SET(_err, ERR_GENERIC, "Table '%s' does not exist.", _tableName);
    }
    pthread_mutex_unlock(&m_fsCtx->tablesMap->mtx);
}

uint16_t fs_table_handle(const char* _tableName)
{
    table_t* table;

    if (cx_cdict_get(m_fsCtx->tablesMap, _tableName, (void**)&table))
    {
        return table->handle;
    }
    return INVALID_HANDLE;
}

bool fs_table_create(table_t** _outTable, const char* _tableName, uint8_t _consistency, uint16_t _partitions, uint32_t _compactionInterval, cx_err_t* _err)
{
    CX_CHECK(strlen(_tableName) > 0, "Invalid _tableName!");
    CX_ERR_CLEAR(_err);

    bool success = false;
    cx_path_t path;
    t_config* meta = NULL;

    if (fs_table_init(_outTable, _tableName, _err))
    {
        if (cx_cdict_tryadd(m_fsCtx->tablesMap, _tableName, (*_outTable), NULL))
        {
            cx_file_path(&path, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES, _tableName);
            if (cx_file_mkdir(&path, _err))
            {
                cx_file_path(&path, "%s/%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES, _tableName, LFS_DIR_METADATA);
                if (cx_file_touch(&path, _err))
                {
                    meta = config_create(path);
                    if (NULL != meta)
                    {
                        char temp[32];

                        // create initial metadata
                        cx_str_from_uint8(_consistency, temp, sizeof(temp));
                        config_set_value(meta, "consistency", temp);
                        cx_str_from_uint16(_partitions, temp, sizeof(temp));
                        config_set_value(meta, "partitionsCount", temp);
                        cx_str_from_uint32(_compactionInterval, temp, sizeof(temp));
                        config_set_value(meta, "compactionInterval", temp);
                        config_save(meta);

                        if (fs_table_meta_get(_tableName, &(*_outTable)->meta, _err)
                            && memtable_init(_tableName, true, &(*_outTable)->memtable, _err))
                        {
                            fs_file_t partFile;
                            for (uint16_t i = 0; i < (*_outTable)->meta.partitionsCount; i++)
                            {
                                CX_MEM_ZERO(partFile);
                                partFile.size = 0;
                                partFile.blocksCount = fs_block_alloc(1, partFile.blocks);

                                if (1 == partFile.blocksCount)
                                {
                                    if (!fs_table_part_set(_tableName, i, false, &partFile, _err))
                                    {
                                        CX_ERR_SET(_err, 1, "Partition #%d for table '%s' could not be written!", i, _tableName);
                                        break;
                                    }
                                }
                                else
                                {
                                    CX_ERR_SET(_err, 1, "An initial block for table '%s' partition #%d could not be allocated."
                                        "we may have ran out of blocks!", _tableName, i);
                                    break;
                                }
                            }

                            success = true;
                        }
                    }
                    else
                    {
                        CX_ERR_SET(_err, 1, "Table metadata file '%s' could not be created.", path);
                    }
                }
            }
        }
        else
        {
            CX_ERR_SET(_err, 1, "Table '%s' already exists.", _tableName);
        }
    }

    if (NULL != meta) config_destroy(meta);
    return success;
}

bool fs_table_delete(const char* _tableName, table_t** _outTable, cx_err_t* _err)
{
    CX_CHECK(strlen(_tableName) > 0, "invalid _tableName!");
    CX_ERR_CLEAR(_err);

    bool success = false;
    cx_path_t    path;
    table_meta_t meta;
    fs_file_t    part;
    table_t*     table = NULL;

    if (cx_cdict_tryremove(m_fsCtx->tablesMap, _tableName, (void**)&table))
    {
        // free the blocks in use
        if (fs_table_meta_get(_tableName, &meta, _err))
        {
            for (uint32_t i = 0; i < meta.partitionsCount; i++)
            {
                if (fs_table_part_get(_tableName, i, false, &part, _err))
                {
                    fs_block_free(part.blocks, part.blocksCount);
                }
                else
                {
                    CX_WARN(CX_ALW, "Partition #%d from table '%s' could not be retrieved and some blocks may have just been leaked!"
                        "Reason: %s", i, _tableName, _err->desc);
                }
            }
        }
        else
        {
            CX_WARN(CX_ALW, "Metadata file for table '%s' does not exist!", _tableName);
        }
        
        // delete files
        cx_file_path(&path, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES, _tableName);
        cx_file_remove(&path, _err);

        success = true;
    }
    else
    {
        CX_ERR_SET(_err, 1, "Table '%s' does not exist.", _tableName);
    }

    if (NULL != _outTable) (*_outTable) = table;
    return success;
}

bool fs_table_meta_get(const char* _tableName, table_meta_t* _outMeta, cx_err_t* _err)
{
    CX_ERR_CLEAR(_err);
    CX_CHECK_NOT_NULL(_outMeta);
    CX_MEM_ZERO(*_outMeta);

    char* key = "";

    cx_str_copy(_outMeta->name, sizeof(_outMeta->name), _tableName);

    cx_path_t metadataPath;
    cx_file_path(&metadataPath, "%s/%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES, _tableName, LFS_DIR_METADATA);

    t_config* meta = config_create(metadataPath);

    if (NULL != meta)
    {
        key = "consistency";
        if (config_has_property(meta, key))
        {
            _outMeta->consistency = (uint8_t)config_get_int_value(meta, key);
        }
        else
        {
            goto key_missing;
        }

        key = "partitionsCount";
        if (config_has_property(meta, key))
        {
            _outMeta->partitionsCount = (uint16_t)config_get_int_value(meta, key);
        }
        else
        {
            goto key_missing;
        }

        key = "compactionInterval";
        if (config_has_property(meta, key))
        {
            _outMeta->compactionInterval = (uint32_t)config_get_int_value(meta, key);
        }
        else
        {
            goto key_missing;
        }

        config_destroy(meta);
        return true;

    key_missing:
        CX_ERR_SET(_err, 1, "key '%s' is missing in table '%s' metadata file.", key, _tableName);
        config_destroy(meta);
        return false;
    }

    CX_ERR_SET(_err, 1, "table metadata file '%s' is missing or not readable.", metadataPath);
    return false;
}

bool fs_table_part_get(const char* _tableName, uint16_t _partNumber, bool _isDuringCompaction, fs_file_t* _outFile, cx_err_t* _err)
{
    cx_path_t path;
    _fs_get_part_path(&path, _tableName, _partNumber, _isDuringCompaction);

    cx_str_copy(_outFile->path, sizeof(_outFile->path), path);
    return _fs_file_load(_outFile, _err);
}

bool fs_table_part_set(const char* _tableName, uint16_t _partNumber, bool _isDuringCompaction, fs_file_t* _file, cx_err_t* _err)
{
    cx_path_t path;
    _fs_get_part_path(&path, _tableName, _partNumber, _isDuringCompaction);

    cx_str_copy(_file->path, sizeof(_file->path), path);
    return _fs_file_save(_file, _err);
}

bool fs_table_part_delete(const char* _tableName, uint16_t _partNumber, bool _isDuringCompaction, cx_err_t* _err)
{
    fs_file_t part;
    if (fs_table_part_get(_tableName, _partNumber, _isDuringCompaction, &part, _err))
    {
        return fs_file_delete(&part, _err);
    }
    return false;
}

bool fs_table_dump_get(const char* _tableName, uint16_t _dumpNumber, bool _isDuringCompaction, fs_file_t* _outFile, cx_err_t* _err)
{
    cx_path_t path;
    _fs_get_dump_path(&path, _tableName, _dumpNumber, _isDuringCompaction);

    cx_str_copy(_outFile->path, sizeof(_outFile->path), path);
    return _fs_file_load(_outFile, _err);
}

bool fs_table_dump_set(const char* _tableName, uint16_t _dumpNumber, bool _isDuringCompaction, fs_file_t* _file, cx_err_t* _err)
{
    cx_path_t path;
    _fs_get_dump_path(&path, _tableName, _dumpNumber, _isDuringCompaction);

    cx_str_copy(_file->path, sizeof(_file->path), path);
    return _fs_file_save(_file, _err);
}

bool fs_table_dump_delete(const char* _tableName, uint16_t _dumpNumber, bool _isDuringCompaction, cx_err_t* _err)
{
    fs_file_t part;
    if (fs_table_dump_get(_tableName, _dumpNumber, _isDuringCompaction, &part, _err))
    {
        return fs_file_delete(&part, _err);
    }
    return false;
}

uint16_t fs_table_dump_number_next(const char* _tableName)
{
    cx_err_t err;
    uint16_t   dumpNumber = 0;
    uint16_t   dumpNumberLast = 0;
    bool       dumpCompacted = false;
    cx_path_t  filePath;
    cx_path_t  fileName;

    cx_file_explorer_t* explorer = fs_table_explorer(_tableName, &err);
    if (NULL != explorer)
    {
        while (cx_file_explorer_next_file(explorer, &filePath))
        {
            if (fs_is_dump(&filePath, &dumpNumber, &dumpCompacted) && !dumpCompacted && dumpNumber > dumpNumberLast)
                dumpNumberLast = dumpNumber;
        }
        cx_file_explorer_destroy(explorer);

        return dumpNumberLast + 1;
    }

    return 0;
}

bool fs_table_dump_tryenqueue()
{
    char* tableName = NULL;
    table_t* table = NULL;

    pthread_mutex_lock(&m_fsCtx->tablesMap->mtx);
    cx_cdict_iter_begin(m_fsCtx->tablesMap);
    while (cx_cdict_iter_next(m_fsCtx->tablesMap, &tableName, (void**)&table))
    {
        task_t* task = taskman_create(TASK_ORIGIN_INTERNAL_PRIORITY, TASK_WT_DUMP, NULL, INVALID_CID);
        if (NULL != task)
        {
            data_dump_t* data = CX_MEM_STRUCT_ALLOC(data);
            cx_str_copy(data->tableName, sizeof(data->tableName), tableName);

            task->data = data;
            taskman_activate(task);
        }
    }
    cx_cdict_iter_end(m_fsCtx->tablesMap);
    pthread_mutex_unlock(&m_fsCtx->tablesMap->mtx);

    return true;
}

bool fs_table_compact_tryenqueue(const char* _tableName)
{
    bool      success = false;
    table_t*  table = NULL;
    task_t*   task = NULL;

    pthread_mutex_lock(&m_fsCtx->tablesMap->mtx);
    if (fs_table_exists(_tableName, &table))
    {
        if (!table->compacting)
        {
            table->compacting = true;

            task = taskman_create(TASK_ORIGIN_INTERNAL_PRIORITY, TASK_WT_COMPACT, NULL, INVALID_CID);
            if (NULL != task)
            {
                data_compact_t* data = CX_MEM_STRUCT_ALLOC(data);
                
                task->data = data;
                task->table = table;
                taskman_activate(task);
                success = true;
            }
            else
            {
                // task creation failed. skip this task.
                table->compacting = false;
            }
        }
        else
        {
            // we can safely ignore this one
            // we don't really want multiple compactions to be performed at the same time
            CX_INFO("Ignoring compaction for table '%s' (another thread is already compacting it).", table->meta.name);
            success = true;
        }
    }
    else
    {
        // table no longer exists, (table might have been deleted and therefore no longer in use).
        success = true;
    }
    pthread_mutex_unlock(&m_fsCtx->tablesMap->mtx);

    return success;
}

bool fs_table_block(table_t* _table)
{
    cx_reslock_block(&_table->reslock);
    cx_reslock_wait_unused(&_table->reslock);
    return true;
}

void fs_table_unblock(table_t* _table, double* _blockedTime)
{
    // unblock the table and make it available again
    cx_reslock_unblock(&_table->reslock);

    task_t* task = NULL;
    // re-schedule pending tasks in blocked queue
    while (!queue_is_empty(_table->blockedQueue))
    {
        task = queue_pop(_table->blockedQueue);
        taskman_activate(task);
    }

    if (NULL != _blockedTime)
        (*_blockedTime) = cx_reslock_blocked_time(&_table->reslock);
}

void fs_table_free(table_t* _table)
{
    data_free_t* data = CX_MEM_STRUCT_ALLOC(data);
    data->resourceType = RESOURCE_TYPE_TABLE;
    data->resourcePtr = _table;

    task_t* task = taskman_create(TASK_ORIGIN_INTERNAL, TASK_MT_FREE, data, INVALID_CID);
    if (NULL != task)
        taskman_activate(task);
}

cx_file_explorer_t* fs_table_explorer(const char* _tableName, cx_err_t* _err)
{
    cx_path_t path;
    cx_file_path(&path, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES, _tableName);

    cx_file_explorer_t* explorer = cx_file_explorer_init(&path, _err);

    CX_WARN(NULL != explorer, "table directory can't be explored: %s", _err->desc);
    return explorer;
}

uint32_t fs_block_alloc(uint32_t _blocksCount, uint32_t* _outBlocksArr)
{
    pthread_mutex_lock(&m_fsCtx->mtxBlocks);
    uint32_t allocatedBlocks = 0;

    // we'll cast the underlying char array as an uint32 array (4 bytes per element)
    // so that we can iterate it and figure out which blocks are available faster 
    // than checking one bit at a time. 
    // we'll be basically checking 4 bytes (32 bits) at once and comparing the 
    // value against UINT32_MAX (integer in which all the bits are set). 
    // a matching value means that 4-bytes segment of our bit array
    // is full (meaning there're no fs blocks available in there for us to use, 
    // in that case, we'll just continue iterating and checking the next segment
    // until we're able to find a free one). 
    // if the value does not match, that means there's at least 1 bit that is not set
    // (meaning there's a fs block available for us to allocate).

    uint32_t* segments = (uint32_t*)m_fsCtx->blocksMap;
    uint32_t maxSegments = (uint32_t)ceilf((float)m_fsCtx->meta.blocksCount / SEGMENT_BITS);
    uint32_t lastSegmentMaxBit = m_fsCtx->meta.blocksCount % SEGMENT_BITS;
    if (0 == lastSegmentMaxBit) lastSegmentMaxBit = SEGMENT_BITS;

    uint32_t i = 0, j = 0, maxBit = 0;

    while (allocatedBlocks < _blocksCount && i < maxSegments)
    {
        for (; i < maxSegments; i++)
        {
            if (UINT32_MAX != segments[i])
            {
                maxBit = (i == maxSegments - 1)
                    ? lastSegmentMaxBit
                    : SEGMENT_BITS;

                // there's *at least* 1 bit available, let's figure out which one
                for (; j < maxBit; j++)
                {
                    if (!BIT_IS_SET(segments[i], j))
                    {
                        // we found a block available!
                        _outBlocksArr[allocatedBlocks++] = i * SEGMENT_BITS + j;

                        segments[i] |= ((uint32_t)1 << j);
                        fseek(m_fsCtx->blocksFile, i * sizeof(uint32_t), SEEK_SET);
                        fwrite(&segments[i], sizeof(uint32_t), 1, m_fsCtx->blocksFile);

                        // initiate it empty
                        fs_block_write(_outBlocksArr[allocatedBlocks - 1], NULL, 0, NULL);

                        goto block_found;
                    }
                }
            }
            j = 0;
        }

    block_found:;
    }
    fflush(m_fsCtx->blocksFile);

    pthread_mutex_unlock(&m_fsCtx->mtxBlocks);
    return allocatedBlocks;
}

void fs_block_free(uint32_t* _blocksArr, uint32_t _blocksCount)
{
    if (_blocksCount < 1) return;

    pthread_mutex_lock(&m_fsCtx->mtxBlocks);

    cx_path_t blockFilePath;

    uint32_t* segments = (uint32_t*)m_fsCtx->blocksMap;
    uint32_t segmentIndex = 0;
    uint32_t bit = 0;

    for (uint32_t i = 0; i < _blocksCount; i++)
    {
        segmentIndex = (uint32_t)floorf((float)_blocksArr[i] / SEGMENT_BITS);
        bit = _blocksArr[i] % SEGMENT_BITS;

        segments[segmentIndex] &= ~((uint32_t)1 << bit);
        fseek(m_fsCtx->blocksFile, segmentIndex * sizeof(uint32_t), SEEK_SET);
        fwrite(&segments[segmentIndex], sizeof(uint32_t), 1, m_fsCtx->blocksFile);

        // delete the file in the ufs
        _fs_get_block_path(&blockFilePath, _blocksArr[i]);
        cx_file_remove(&blockFilePath, NULL);
    }
    fflush(m_fsCtx->blocksFile);

    pthread_mutex_unlock(&m_fsCtx->mtxBlocks);
}

int32_t fs_block_read(uint32_t _blockNumber, char* _buffer, cx_err_t* _err)
{
    cx_path_t blockFilePath;
    _fs_get_block_path(&blockFilePath, _blockNumber);

    return cx_file_read(&blockFilePath, _buffer, m_fsCtx->meta.blocksSize, _err);
}

bool fs_block_write(uint32_t _blockNumber, char* _buffer, uint32_t _bufferSize, cx_err_t* _err)
{
    CX_CHECK(_bufferSize <= m_fsCtx->meta.blocksSize, "_bufferSize must be less than or equal to blocksSize (%d bytes)!", 
        m_fsCtx->meta.blocksSize);

    cx_path_t blockFilePath;
    _fs_get_block_path(&blockFilePath, _blockNumber);

    if (NULL == _buffer || 0 == _bufferSize)
    {
        return cx_file_touch(&blockFilePath, _err);
    }
    else
    {
        return cx_file_write(&blockFilePath, _buffer, _bufferSize, _err);
    }
}

uint32_t fs_block_size()
{
    return m_fsCtx->meta.blocksSize;
}

bool fs_file_read(fs_file_t* _file, char* _buffer, cx_err_t* _err)
{
    uint32_t buffPos = 0;
    int32_t bytesRead = 0;

    for (uint32_t i = 0; i < _file->blocksCount; i++)
    {
        bytesRead = fs_block_read(_file->blocks[i], &_buffer[buffPos], _err);
        if (-1 == bytesRead)
        {
            CX_ERR_SET(_err, 1, "block #%d could not be read!", _file->blocks[i]);
            return false;
        }

        buffPos += bytesRead;
    }

    if (buffPos != _file->size)
    {
        CX_ERR_SET(_err, 1, "file is not fully loaded! (size is %d but we read %d)", _file->size, buffPos);
        return false;
    }

    return true;
}

bool fs_file_delete(fs_file_t* _file, cx_err_t* _err)
{
    fs_block_free(_file->blocks, _file->blocksCount);

    return cx_file_remove(&_file->path, _err);
}

bool fs_is_dump(cx_path_t* _filePath, uint16_t* _outDumpNumber, bool* _outDuringCompaction)
{
    uint16_t dumpNumber = 0;
    bool     duringCompaction = false;
    
    cx_path_t fileName;
    cx_file_get_name(_filePath, false, &fileName);

    if (cx_str_starts_with(fileName, LFS_DUMP_PREFIX, true))
    {
        duringCompaction = cx_str_ends_with(fileName, "." LFS_DUMP_EXTENSION_COMPACTION, true);

        if (!duringCompaction && !cx_str_ends_with(fileName, "." LFS_DUMP_EXTENSION, true))
            return false;

        (*strchr(fileName, '.')) = '\0'; // get rid of the extension truncating at char '.'
        if (cx_str_to_uint16(&fileName[sizeof(LFS_DUMP_PREFIX) - 1], &dumpNumber))
        {
            if (NULL != _outDumpNumber) (*_outDumpNumber) = dumpNumber;
            if (NULL != _outDuringCompaction) (*_outDuringCompaction) = duringCompaction;
            return true;
        }
    }
    return false;
}

 /****************************************************************************************
  ***  PRIVATE FUNCTIONS
  ***************************************************************************************/

static bool _fs_is_lfs(cx_path_t* _rootDir)
{
    cx_path_t lfsFile;
    cx_file_path(&lfsFile, "%s/%s", _rootDir, LFS_ROOT_FILE_MARKER);
    
    return true
        && cx_file_exists(&lfsFile)
        && !cx_file_is_folder(&lfsFile);
}

static uint32_t _fs_calc_bitmap_size(uint32_t _maxBlocks)
{
    // returns the minimumn amount of bytes needed, to store at least
    // _maxBlocks amount of bits in our bitmap file which represents blocks
    // availability in our fake filesystem.
    return _maxBlocks / CHAR_BIT + (_maxBlocks % CHAR_BIT > 0 ? 1 : 0);
}

static bool _fs_bootstrap(cx_path_t* _rootDir, uint32_t _maxBlocks, uint32_t _blockSize, cx_err_t* _err)
{
    char temp[256];
    bool success = true;
    cx_path_t path;

    CX_INFO("bootstrapping lissandra filesystem in %s...", _rootDir);
    
    // create directory structure
    cx_file_path(&path, "%s/%s", _rootDir, LFS_ROOT_FILE_MARKER);
    success = success && cx_file_touch(&path, _err);

    cx_file_path(&path, "%s/%s", _rootDir, LFS_DIR_METADATA);
    success = success && cx_file_mkdir(&path, _err);

    cx_file_path(&path, "%s/%s", _rootDir, LFS_DIR_TABLES);
    success = success && cx_file_mkdir(&path, _err);

    cx_file_path(&path, "%s/%s", _rootDir, LFS_DIR_BLOCKS);
    success = success && cx_file_mkdir(&path, _err);

    // create metadata file
    if (success)
    {
        success = false;

        cx_file_path(&path, "%s/%s/%s", _rootDir, LFS_DIR_METADATA, LFS_FILE_METADATA);
        cx_file_touch(&path, _err);

        t_config* meta = config_create(path);
        if (NULL != meta)
        {
            cx_str_from_uint32(_maxBlocks, temp, sizeof(temp));
            config_set_value(meta, LFS_META_PROP_BLOCKS_COUNT, temp);

            cx_str_from_uint32(_blockSize, temp, sizeof(temp));
            config_set_value(meta, LFS_META_PROP_BLOCKS_SIZE, temp);

            config_set_value(meta, LFS_META_PROP_MAGIC_NUMBER, LFS_MAGIC_NUMBER);

            config_save(meta);
            config_destroy(meta);

            success = true;
        }
        else
        {
            CX_ERR_SET(_err, ERR_INIT_FS_BOOTSTRAP,
                "configuration file '%s' could not be created.", path);
        }
    }

    // initialize bitmap file with all the blocks available (all bits set to zero)
    if (success)
    {
        success = false;

        uint32_t bytesNeeded = _fs_calc_bitmap_size(_maxBlocks);
        char* emptyBuffer = malloc(bytesNeeded);
        memset(emptyBuffer, 0, bytesNeeded);

        cx_file_path(&path, "%s/%s/%s", _rootDir, LFS_DIR_METADATA, LFS_FILE_BITMAP);
        success = cx_file_write(&path, emptyBuffer, bytesNeeded, _err);
        
        free(emptyBuffer);
    }

    return success;
}

static bool _fs_load_meta(cx_err_t* _err)
{
    CX_ERR_CLEAR(_err);

    char* temp = NULL;
    char* key = "";
    
    cx_path_t metadataPath;
    cx_file_path(&metadataPath, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_METADATA, LFS_FILE_METADATA);

    t_config* meta = config_create(metadataPath);

    if (NULL != meta)
    {
        key = LFS_META_PROP_BLOCKS_COUNT;
        if (config_has_property(meta, key))
        {
            m_fsCtx->meta.blocksCount = (uint32_t)config_get_int_value(meta, key);
        }
        else
        {
            goto key_missing;
        }

        key = LFS_META_PROP_BLOCKS_SIZE;
        if (config_has_property(meta, key))
        {
            m_fsCtx->meta.blocksSize = (uint32_t)config_get_int_value(meta, key);
        }
        else
        {
            goto key_missing;
        }

        key = LFS_META_PROP_MAGIC_NUMBER;
        if (config_has_property(meta, key))
        {
            temp = config_get_string_value(meta, key);
            cx_str_copy(m_fsCtx->meta.magicNumber, sizeof(m_fsCtx->meta.magicNumber), temp);
        }
        else
        {
            goto key_missing;
        }

        config_destroy(meta);

        if ((0 == strcmp(m_fsCtx->meta.magicNumber, LFS_MAGIC_NUMBER)))
        {
            return true;
        }
        else
        {
            CX_ERR_SET(_err, ERR_INIT_FS_META, "invalid metadata magic number '%s'.", m_fsCtx->meta.magicNumber);
        }

    key_missing:
        CX_ERR_SET(_err, ERR_INIT_FS_META, "key '%s' is missing in the filesystem metadata file '%s'.", key, metadataPath);
        config_destroy(meta);
    }
    else
    {
        CX_ERR_SET(_err, ERR_INIT_FS_META, "filesystem metadata '%s' is missing or not readable.", metadataPath);
    }
    
    return false;
}

static bool _fs_load_tables(cx_err_t* _err)
{
    CX_ERR_CLEAR(_err);

    m_fsCtx->tablesMap = cx_cdict_init();
    if (NULL == m_fsCtx->tablesMap)
    {
        CX_ERR_SET(_err, ERR_INIT_CDICT, "tablesMap concurrent dictionary creation failed.");
        return false;
    }

    cx_path_t tablesPath;
    cx_file_path(&tablesPath, "%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES);
    // not really needed, but just in case the tables folder is deleted, we can recover
    cx_file_mkdir(&tablesPath, _err);
    
    cx_file_explorer_t* explorer = cx_file_explorer_init(&tablesPath, _err);
    cx_path_t tableFolderPath;
    cx_path_t tableName;
    table_t* table = NULL;
    uint32_t count = 0;

    if (NULL != explorer)
    {
        while (cx_file_explorer_next_folder(explorer, &tableFolderPath))
        {
            cx_file_get_name(&tableFolderPath, false, &tableName);
            
            if (fs_table_init(&table, tableName, _err)
                && fs_table_meta_get(tableName, &table->meta, _err)
                && memtable_init(tableName, true, &table->memtable, _err))
            {
                table->timerHandle = cx_timer_add(table->meta.compactionInterval, LFS_TIMER_COMPACT, table);
                CX_CHECK(INVALID_HANDLE != table->timerHandle, "we ran out of timer handles for table '%s'!", table->meta.name);
                
                // unlock the resource (our table initiates blocked)
                cx_reslock_unblock(&table->reslock);

                cx_cdict_set(m_fsCtx->tablesMap, tableName, table);
                count++;
            }
            else
            {
                CX_WARN(CX_ALW, "Table '%s' skipped. %s", _err->desc);
            }
        }

        cx_file_explorer_destroy(explorer);

        CX_INFO("%d tables imported from the filesystem", count);
        return true;
    }

    CX_ERR_SET(_err, ERR_INIT_FS_TABLES,
        "Tables directory '%s' is not accessible.", tablesPath);
    return false;
}

static bool _fs_load_blocks(cx_err_t* _err)
{
    cx_path_t bitmapPath;
    cx_file_path(&bitmapPath, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_METADATA, LFS_FILE_BITMAP);

    int32_t size = (int32_t)_fs_calc_bitmap_size(m_fsCtx->meta.blocksCount);
    m_fsCtx->blocksMap = malloc(size);
    
    int32_t bytesRead = cx_file_read(&bitmapPath, m_fsCtx->blocksMap, size, _err);
    if (size == bytesRead)
    {
        // the bitmap file is OK
        m_fsCtx->blocksFile = fopen(bitmapPath, "r+");
        if (NULL == m_fsCtx->blocksFile)
        {
            CX_ERR_SET(_err, ERR_INIT_FS_BITMAP, "bitmap file '%s' could not be opened for writing!", bitmapPath);
        }
        else
        {
            return true;
        }
    }
    else if (-1 == bytesRead)
    {
        // noop. _err already contains the reason of the cx_file_read call failure.
    }
    else
    {
        CX_ERR_SET(_err, ERR_INIT_FS_BITMAP, "the amount of bytes expected and the "
            "amount of actual bytes read from the bitmap file '%s' do not match. "
            "the file might be corrupt at this point.", bitmapPath);
    }

    if (NULL != m_fsCtx->blocksMap) 
    {
        free(m_fsCtx->blocksMap);
        m_fsCtx->blocksMap = NULL;
    }

    if (NULL != m_fsCtx->blocksFile)
    {
        fclose(m_fsCtx->blocksFile);
        m_fsCtx->blocksFile = NULL;
    }
    
    return false;
}

bool fs_table_init(table_t** _outTable, const char* _tableName, cx_err_t* _err)
{
    CX_CHECK(strlen(_tableName) > 0, "Invalid table name!");
    
    bool success = false;
    table_t* table = CX_MEM_STRUCT_ALLOC(table);
   
    table->timerHandle = INVALID_HANDLE;
    
    table->blockedQueue = queue_create();
    CX_CHECK_NOT_NULL(table->blockedQueue);
        
    success = true 
        && NULL != table->blockedQueue
        && cx_reslock_init(&table->reslock, true);

    if (!success)
    {
        (*_outTable) = NULL;
        fs_table_destroy(table);
        CX_ERR_SET(_err, ERR_GENERIC, "Table initialization failed.")
    }
    else
    {
        (*_outTable) = table;
    }

    return success;
}

void fs_table_destroy(table_t* _table)
{
    // this function is not thread-safe. it must only be called from the main thread!

    if (NULL != _table)
    {
        if (INVALID_HANDLE != _table->timerHandle)
        {
            cx_timer_remove(_table->timerHandle);
            _table->timerHandle = INVALID_HANDLE;
        }

        memtable_destroy(&_table->memtable);
        cx_reslock_destroy(&_table->reslock);
        queue_clean(_table->blockedQueue);

        if (NULL != _table->blockedQueue)
        {
            queue_destroy(_table->blockedQueue);
            _table->blockedQueue = NULL;
        }

        free(_table);
    }
}

static bool _fs_file_load(fs_file_t* _outFile, cx_err_t* _err)
{
    bool success = false;
    char* key = NULL;
    bool keyMissing = false;
    t_config* file = NULL;
    CX_ERR_CLEAR(_err);

    if (cx_file_exists(&_outFile->path))
    {
        file = config_create(_outFile->path);
        if (NULL != file)
        {
            key = LFS_FILE_PROP_SIZE;
            if (config_has_property(file, key))
            {
                _outFile->size = (uint32_t)config_get_int_value(file, key);
            }
            else
            {
                keyMissing = true;
                goto key_missing;
            }

            key = LFS_FILE_PROP_BLOCKS;
            if (config_has_property(file, key))
            {
                char** blocks = config_get_array_value(file, key);

                uint32_t i = 0, j = 0;
                bool finished = (NULL == blocks[i]);
                while (!finished && j < CX_ARR_SIZE(_outFile->blocks))
                {
                    cx_str_to_uint32(blocks[i], &(_outFile->blocks[j++]));
                    free(blocks[i++]);
                    finished = (NULL == blocks[i]);
                }
                free(blocks);

                CX_CHECK(finished, "there're still pending blocks to read! static buffer of %d elements is not enough!",
                    CX_ARR_SIZE(_outFile->blocks));

                _outFile->blocksCount = j;
            }
            else
            {
                keyMissing = true;
                goto key_missing;
            }

            success = true;

        key_missing:
            if (keyMissing)
            {
                CX_ERR_SET(_err, 1, "File '%s' is corrupt. Key '%s' is missing.", _outFile->path, key);
            }
        }
        else
        {
            CX_ERR_SET(_err, 1, "File '%s' could not be loaded!", _outFile->path);
        }
    }
    else
    {
        CX_ERR_SET(_err, 1, "File '%s' does not exist.", _outFile->path);
    }

    if (NULL != file) config_destroy(file);
    return success;
}

static bool _fs_file_save(fs_file_t* _file, cx_err_t* _err)
{
    bool success = false;
    t_config* file = NULL;
    CX_ERR_CLEAR(_err);
    char temp[32];

    if (cx_file_remove(&_file->path, _err) && cx_file_touch(&_file->path, _err))
    {
        file = config_create(_file->path);
        if (NULL != file)
        {
            cx_str_from_uint32(_file->size, temp, sizeof(temp));
            config_set_value(file, LFS_FILE_PROP_SIZE, temp);

            if (_file->blocksCount > 0)
            {
                char* buff = cx_str_copy_d("");
                char* buffPrev = NULL;
                for (uint32_t i = 0; i < _file->blocksCount; i++)
                {
                    cx_str_format(temp, sizeof(temp), ",%d", _file->blocks[i]);
                    buffPrev = buff;
                    buff = cx_str_cat_d(buffPrev, temp);
                    free(buffPrev);
                }
                buffPrev = buff;
                buff = cx_str_format_d("[%s]", &(buffPrev[1]));

                config_set_value(file, LFS_FILE_PROP_BLOCKS, buff);

                config_save(file);
                free(buffPrev);
                free(buff);
            }
            else
            {
                config_set_value(file, LFS_FILE_PROP_BLOCKS, "[]");
            }

            success = true;
        }
        else
        {
            CX_ERR_SET(_err, 1, "file '%s' could not be loaded!", _file->path);
        }
    }

    if (NULL != file) config_destroy(file);
    return success;
}

static void _fs_get_dump_path(cx_path_t* _outFilePath, const char* _tableName, uint16_t _dumpNumber, bool _isDuringCompaction)
{
    cx_file_path(_outFilePath, "%s/%s/%s/%s%d.%s", m_fsCtx->rootDir, LFS_DIR_TABLES,
        _tableName, LFS_DUMP_PREFIX, _dumpNumber,
        _isDuringCompaction ? LFS_DUMP_EXTENSION_COMPACTION : LFS_DUMP_EXTENSION);
}

static void _fs_get_part_path(cx_path_t* _outFilePath, const char* _tableName, uint16_t _partNumber, bool _isDuringCompaction)
{
    cx_file_path(_outFilePath, "%s/%s/%s/%s%d.%s", m_fsCtx->rootDir, LFS_DIR_TABLES,
        _tableName, LFS_PART_PREFIX, _partNumber,
        _isDuringCompaction ? LFS_PART_EXTENSION_COMPACTION : LFS_PART_EXTENSION);
}

static void _fs_get_block_path(cx_path_t* _outFilePath, uint32_t _blockNumber)
{
    cx_file_path(_outFilePath, "%s/%s/%s%d.%s", m_fsCtx->rootDir, LFS_DIR_BLOCKS,
        LFS_BLOCK_PREFIX, _blockNumber, LFS_BLOCK_EXTENSION);
}