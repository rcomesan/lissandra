#include "fs.h"

#include "memtable.h"

#include <cx/mem.h>
#include <cx/fs.h>
#include <cx/str.h>
#include <cx/math.h>

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

static bool         _fs_bootstrap(cx_path_t* _rootDir, uint32_t _maxBlocks, uint32_t _blockSize, cx_error_t* _err);

static bool         _fs_load_meta(cx_error_t* _err);

static bool         _fs_load_tables(cx_error_t* _err);

static bool         _fs_load_blocks(cx_error_t* _err);

static bool         _fs_table_init(table_t* _table, const char* _tableName, cx_error_t* _err);

static bool         _fs_file_write(cx_path_t* _filePath, fs_file_t* _outFile, cx_error_t* _err);

static bool         _fs_file_read(cx_path_t* _filePath, fs_file_t* _file, cx_error_t* _err);


/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool fs_init(cx_error_t* _err)
{
    CX_CHECK(NULL == m_fsCtx, "fs is already initialized!");

    m_fsCtx = CX_MEM_STRUCT_ALLOC(m_fsCtx);
    CX_MEM_ZERO(*_err);

    bool rootDirOk = false;
    cx_path_t rootDir;
    cx_fs_path(&rootDir, g_ctx.cfg.rootDir);

    if (cx_fs_exists(&rootDir))
    {
        if (cx_fs_is_folder(&rootDir))
        {
            rootDirOk = _fs_is_lfs(&rootDir);
            if (!rootDirOk)
            {
                CX_ERROR_SET(_err, LFS_ERR_INIT_FS_ROOTDIR,
                    "the given root dir '%s' exists but it's not a lissandra file system (%s file is missing).",
                    rootDir, LFS_ROOT_FILE_MARKER);
            }
        }
        else
        {
            rootDirOk = false;
            CX_ERROR_SET(_err, LFS_ERR_INIT_FS_ROOTDIR, "the given root dir '%s' already exists and is a file!", rootDir);
        }
    }
    else
    {
        rootDirOk = _fs_bootstrap(&rootDir, g_ctx.cfg.blocksCount, g_ctx.cfg.blocksSize, _err);
    }

    if (rootDirOk)
    {
        cx_str_copy(m_fsCtx->rootDir, sizeof(m_fsCtx->rootDir), rootDir);
        CX_INFO("filesystem mount point: %s", m_fsCtx->rootDir);
        
        m_fsCtx->mtxCreateDropInit = (0 == pthread_mutex_init(&m_fsCtx->mtxCreateDrop, NULL));
        m_fsCtx->mtxBlocksInit = (0 == pthread_mutex_init(&m_fsCtx->mtxBlocks, NULL));

        if (!m_fsCtx->mtxCreateDropInit || !m_fsCtx->mtxBlocksInit)
        {
            CX_ERROR_SET(_err, 1, "pthread mutex initialization failed!");
        }

        return true
            && m_fsCtx->mtxCreateDropInit
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
    
    // destroy tablesMap
    cx_cdict_destroy(m_fsCtx->tablesMap, NULL);

    // mutexes
    if (m_fsCtx->mtxCreateDropInit)
    {
        pthread_mutex_destroy(&m_fsCtx->mtxCreateDrop);
        m_fsCtx->mtxCreateDropInit = false;
    }
    if (m_fsCtx->mtxBlocksInit)
    {
        pthread_mutex_destroy(&m_fsCtx->mtxBlocks);
        m_fsCtx->mtxBlocksInit = false;
    }

    free(m_fsCtx);
}

table_meta_t* fs_describe(uint16_t* _outTablesCount, cx_error_t* _err)
{
    table_meta_t* tables = NULL;
    char* key = NULL;
    table_t* table;
    uint16_t i = 0;

    pthread_mutex_lock(&m_fsCtx->mtxCreateDrop);

    (*_outTablesCount) = (uint16_t)cx_cdict_size(m_fsCtx->tablesMap);

    cx_cdict_iter_begin(m_fsCtx->tablesMap);
    if (0 < (*_outTablesCount))
    {
        tables = CX_MEM_ARR_ALLOC(tables, (*_outTablesCount));

        while (cx_cdict_iter_next(m_fsCtx->tablesMap, &key, (void**)&table))
        {
            if (!table->deleted)
                memcpy(&(tables[i++]), &(table->meta), sizeof(tables[0]));
        }
    }
    cx_cdict_iter_end(m_fsCtx->tablesMap);

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

    pthread_mutex_unlock(&m_fsCtx->mtxCreateDrop);
    return tables;
}

bool fs_table_blocked_guard(const char* _tableName, cx_error_t* _err, pthread_mutex_t* _mtx)
{
    table_t* table;

    bool isBlocked = true
        && fs_table_exists(_tableName, &table)
        && table->blocked;

    if (isBlocked)
    {
        CX_ERROR_SET(_err, LFS_ERR_TABLE_BLOCKED, "Operation cannot be performed at this time since the table is blocked. Try agian later.");
        if (NULL != _mtx)
        {
            pthread_mutex_unlock(_mtx);
        }
        return true;
    }
    return false;
}

bool fs_table_exists(const char* _tableName, table_t** _outTable)
{
    table_t* table;

    if (cx_cdict_get(m_fsCtx->tablesMap, _tableName, (void**)&table) && !table->deleted)
    {
        if (NULL != _outTable)
        {
            (*_outTable) = table;
        }
        return true;
    }
    return false;
}

bool fs_table_create(table_t* _table, const char* _tableName, uint8_t _consistency, uint16_t _partitions, uint32_t _compactionInterval, cx_error_t* _err)
{
    CX_CHECK(strlen(_tableName) > 0, "invalid _tableName!");
    CX_MEM_ZERO(*_err);

    pthread_mutex_lock(&m_fsCtx->mtxCreateDrop);
    if (fs_table_blocked_guard(_tableName, _err, &m_fsCtx->mtxCreateDrop)) return false;

    cx_path_t path;
    t_config* meta = NULL;

    if (!fs_table_exists(_tableName, NULL))
    {
        cx_fs_path(&path, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES, _tableName);
        if (cx_fs_mkdir(&path, _err))
        {
            cx_fs_path(&path, "%s/%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES, _tableName, LFS_DIR_METADATA);
            if (cx_fs_touch(&path, _err))
            {
                meta = config_create(path);
                if (NULL != meta)
                {
                    char temp[32];

                    cx_str_from_uint8(_consistency, temp, sizeof(temp));
                    config_set_value(meta, "consistency", temp);

                    cx_str_from_uint16(_partitions, temp, sizeof(temp));
                    config_set_value(meta, "partitionsCount", temp);

                    cx_str_from_uint32(_compactionInterval, temp, sizeof(temp));
                    config_set_value(meta, "compactionInterval", temp);

                    config_save(meta);

                    if (_fs_table_init(_table, _tableName, _err))
                    {
                        fs_file_t partFile;
                        for (uint16_t i = 0; i < _table->meta.partitionsCount && ERR_NONE == _err->code; i++)
                        {
                            CX_MEM_ZERO(partFile);
                            partFile.size = 0;
                            partFile.blocksCount = fs_block_alloc(1, partFile.blocks);

                            if (1 == partFile.blocksCount)
                            {
                                if (!fs_table_set_part(_tableName, i, &partFile, _err))
                                {
                                    CX_ERROR_SET(_err, 1, "partition #%d for table '%s' could not be written!", i, _tableName);
                                }
                            }
                            else
                            {
                                CX_ERROR_SET(_err, 1, "an initial block for table '%s' partition #%d could not be allocated."
                                    "we may have ran out of blocks!", _tableName, i);
                            }
                        }

                        if (ERR_NONE == _err->code)
                            cx_cdict_set(m_fsCtx->tablesMap, _tableName, _table);
                    }
                }
                else
                {
                    CX_ERROR_SET(_err, 1, "table metadata file '%s' could not be created.", path);
                }
            }
        }
    }
    else
    {
        CX_ERROR_SET(_err, 1, "Table '%s' already exists.", _tableName);
    }
    
    if (NULL != meta) config_destroy(meta);

    // if failed, mark table as deleted so that it gets wiped out in the main loop
    if (ERR_NONE != _err->code) _table->deleted = true; 

    pthread_mutex_unlock(&m_fsCtx->mtxCreateDrop);
    return (ERR_NONE == _err->code);
}

bool fs_table_delete(const char* _tableName, cx_error_t* _err)
{
    CX_CHECK(strlen(_tableName) > 0, "invalid _tableName!");
    CX_MEM_ZERO(*_err);

    pthread_mutex_lock(&m_fsCtx->mtxCreateDrop);
    if (fs_table_blocked_guard(_tableName, _err, &m_fsCtx->mtxCreateDrop)) return false;

    cx_path_t path;
    table_meta_t meta;
    fs_file_t part;
    table_t* table;

    if (fs_table_exists(_tableName, &table))
    {
        // remove from dict
        cx_cdict_erase(m_fsCtx->tablesMap, _tableName, NULL);
        // mark the table slot as deleted, so that it gets freed up safely in the main loop
        table->deleted = true;

        // free the blocks in use
        if (fs_table_get_meta(_tableName, &meta, _err))
        {
            for (uint32_t i = 0; i < meta.partitionsCount; i++)
            {
                if (fs_table_get_part(_tableName, i, &part, _err))
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
        cx_fs_path(&path, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES, _tableName);
        cx_fs_remove(&path, _err);
    }
    else
    {
        CX_ERROR_SET(_err, 1, "Table '%s' does not exist.", _tableName);
    }

    pthread_mutex_unlock(&m_fsCtx->mtxCreateDrop);
    return (ERR_NONE == _err->code);
}

bool fs_table_get_meta(const char* _tableName, table_meta_t* _outMeta, cx_error_t* _err)
{
    CX_MEM_ZERO(*_err);
    CX_CHECK_NOT_NULL(_outMeta);
    CX_MEM_ZERO(*_outMeta);

    char* key = "";

    cx_str_copy(_outMeta->name, sizeof(_outMeta->name), _tableName);

    cx_path_t metadataPath;
    cx_fs_path(&metadataPath, "%s/%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES, _tableName, LFS_DIR_METADATA);

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
        CX_ERROR_SET(_err, 1, "key '%s' is missing in table '%s' metadata file.", key, _tableName);
        config_destroy(meta);
    }

    CX_ERROR_SET(_err, 1, "table metadata file '%s' is missing or not readable.", metadataPath);
    return false;
}

bool fs_table_get_part(const char* _tableName, uint16_t _partNumber, fs_file_t* _outFile, cx_error_t* _err)
{
    cx_path_t path;
    cx_fs_path(&path, "%s/%s/%s/%s%d." LFS_PART_EXTENSION, m_fsCtx->rootDir, LFS_DIR_TABLES,
        _tableName, LFS_PART_PREFIX, _partNumber);

    return _fs_file_read(&path, _outFile, _err);
}

bool fs_table_set_part(const char* _tableName, uint16_t _partNumber, fs_file_t* _file, cx_error_t* _err)
{
    cx_path_t path;
    cx_fs_path(&path, "%s/%s/%s/%s%d." LFS_PART_EXTENSION, m_fsCtx->rootDir, LFS_DIR_TABLES,
        _tableName, LFS_PART_PREFIX, _partNumber);

    return _fs_file_write(&path, _file, _err);
}

bool fs_table_get_dump(const char* _tableName, uint16_t _dumpNumber, bool _isDuringCompaction, fs_file_t* _outFile, cx_error_t* _err)
{
    cx_path_t path;
    cx_fs_path(&path, "%s/%s/%s/%s%d.%s", m_fsCtx->rootDir, LFS_DIR_TABLES,  
        _tableName, LFS_DUMP_PREFIX, _dumpNumber,
        _isDuringCompaction ? LFS_DUMP_EXTENSION_COMPACTION : LFS_DUMP_EXTENSION);

    return _fs_file_read(&path, _outFile, _err);
}

bool fs_table_set_dump(const char* _tableName, uint16_t _dumpNumber, fs_file_t* _file, cx_error_t* _err)
{
    cx_path_t path;
    cx_fs_path(&path, "%s/%s/%s/%s%d." LFS_DUMP_EXTENSION, m_fsCtx->rootDir, LFS_DIR_TABLES, 
        _tableName, LFS_DUMP_PREFIX, _dumpNumber);

    return _fs_file_write(&path, _file, _err);
}

uint16_t fs_table_get_dump_number_next(const char* _tableName)
{
    cx_error_t err;
    uint16_t   dumpNumber = 0;
    uint16_t   dumpNumberLast = 0;
    bool       dumpCompacted = false;
    cx_path_t  filePath;
    cx_path_t  fileName;

    cx_fs_explorer_t* explorer = fs_table_explorer(_tableName, &err);
    if (NULL != explorer)
    {
        while (cx_fs_explorer_next_file(explorer, &filePath))
        {
            if (fs_is_dump(&filePath, &dumpNumber, &dumpCompacted) && !dumpCompacted && dumpNumber > dumpNumberLast)
                dumpNumberLast = dumpNumber;
        }
        cx_fs_explorer_destroy(explorer);

        return dumpNumberLast + 1;
    }

    return 0;
}

cx_fs_explorer_t* fs_table_explorer(const char* _tableName, cx_error_t* _err)
{
    cx_path_t path;
    cx_fs_path(&path, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES, _tableName);

    cx_fs_explorer_t* explorer = cx_fs_explorer_init(&path, _err);

    CX_WARN(NULL != explorer, "table directory can't be explored: %s", _err->desc);
    return explorer;
}


uint32_t fs_block_alloc(uint32_t _blocksCount, uint32_t* _outBlocksArr)
{
    pthread_mutex_lock(&m_fsCtx->mtxBlocks);
    uint32_t allocatedBlocks = 0;

    cx_path_t bitmapFilePath;
    cx_fs_path(&bitmapFilePath, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_METADATA, LFS_FILE_BITMAP);

    FILE* bitmap = fopen(bitmapFilePath, "r+");
    if (NULL != bitmap)
    {
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
                            fseek(bitmap, i * sizeof(uint32_t), SEEK_SET);
                            fwrite(&segments[i], sizeof(uint32_t), 1, bitmap);

                            goto block_found;
                        }
                    }
                }
                j = 0;
            }

        block_found:;
        }
        fflush(bitmap);
        fclose(bitmap);
    }
    CX_CHECK(NULL != bitmap, "bitmap file '%s' could not be opened for writing!", bitmapFilePath);

    pthread_mutex_unlock(&m_fsCtx->mtxBlocks);
    return allocatedBlocks;
}

void fs_block_free(uint32_t* _blocksArr, uint32_t _blocksCount)
{
    if (_blocksCount < 1) return;

    pthread_mutex_lock(&m_fsCtx->mtxBlocks);

    cx_path_t bitmapFilePath;
    cx_fs_path(&bitmapFilePath, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_METADATA, LFS_FILE_BITMAP);

    FILE* bitmap = fopen(bitmapFilePath, "r+");
    if (NULL != bitmap)
    {
        uint32_t* segments = (uint32_t*)m_fsCtx->blocksMap;
        uint32_t segmentIndex = 0;
        uint32_t bit = 0;

        for (uint32_t i = 0; i < _blocksCount; i++)
        {
            segmentIndex = (uint32_t)floorf((float)_blocksArr[i] / SEGMENT_BITS);
            bit = _blocksArr[i] % SEGMENT_BITS;

            segments[segmentIndex] &= ~((uint32_t)1 << bit);
            fseek(bitmap, segmentIndex * sizeof(uint32_t), SEEK_SET);
            fwrite(&segments[segmentIndex], sizeof(uint32_t), 1, bitmap);
        }
        fflush(bitmap);
        fclose(bitmap);
    }
    CX_CHECK(NULL != bitmap, "bitmap file '%s' could not be opened for writing!", bitmapFilePath);

    pthread_mutex_unlock(&m_fsCtx->mtxBlocks);
}

int32_t fs_block_read(uint32_t _blockNumber, char* _buffer, cx_error_t* _err)
{
    cx_path_t blockFilePath;
    cx_fs_path(&blockFilePath, "%s/%s/%d.bin", m_fsCtx->rootDir, LFS_DIR_BLOCKS, _blockNumber);

    return cx_fs_read(&blockFilePath, _buffer, m_fsCtx->meta.blocksSize, _err);
}

bool fs_block_write(uint32_t _blockNumber, char* _buffer, uint32_t _bufferSize, cx_error_t* _err)
{
    CX_CHECK(_bufferSize <= m_fsCtx->meta.blocksSize, "_bufferSize must be less than or equal to blocksSize (%d bytes)!", 
        m_fsCtx->meta.blocksSize);

    cx_path_t blockFilePath;
    cx_fs_path(&blockFilePath, "%s/%s/%d.bin", m_fsCtx->rootDir, LFS_DIR_BLOCKS, _blockNumber);

    return cx_fs_write(&blockFilePath, _buffer, _bufferSize, _err);
}

uint32_t fs_block_size()
{
    return m_fsCtx->meta.blocksSize;
}

bool fs_file_load(fs_file_t* _file, char* _buffer, cx_error_t* _err)
{
    uint32_t buffPos = 0;
    int32_t bytesRead = 0;

    for (uint32_t i = 0; i < _file->blocksCount; i++)
    {
        bytesRead = fs_block_read(_file->blocks[i], &_buffer[buffPos], _err);
        if (-1 == bytesRead)
        {
            CX_ERROR_SET(_err, 1, "block #%d could not be read!", _file->blocks[i]);
            return false;
        }

        buffPos += bytesRead;
    }

    if (buffPos != _file->size)
    {
        CX_ERROR_SET(_err, 1, "file is not fully loaded! (size is %d but we read %d)", _file->size, buffPos);
        return false;
    }

    return true;
}

bool fs_is_dump(cx_path_t* _filePath, uint16_t* _outDumpNumber, bool* _outDuringCompaction)
{
    uint16_t dumpNumber = 0;
    bool     duringCompaction = false;
    
    cx_path_t fileName;
    cx_fs_get_name(_filePath, false, &fileName);

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
    cx_fs_path(&lfsFile, "%s/%s", _rootDir, LFS_ROOT_FILE_MARKER);
    
    return true
        && cx_fs_exists(&lfsFile)
        && !cx_fs_is_folder(&lfsFile);
}

static uint32_t _fs_calc_bitmap_size(uint32_t _maxBlocks)
{
    // returns the minimumn amount of bytes needed, to store at least
    // _maxBlocks amount of bits in our bitmap file which represents blocks
    // availability in our fake filesystem.
    return _maxBlocks / CHAR_BIT + (_maxBlocks % CHAR_BIT > 0 ? 1 : 0);
}

static bool _fs_bootstrap(cx_path_t* _rootDir, uint32_t _maxBlocks, uint32_t _blockSize, cx_error_t* _err)
{
    char temp[256];
    bool success = true;
    cx_path_t path;

    CX_INFO("bootstrapping lissandra filesystem in %s...", _rootDir);
    
    // create directory structure
    cx_fs_path(&path, "%s/%s", _rootDir, LFS_ROOT_FILE_MARKER);
    success = success && cx_fs_touch(&path, _err);

    cx_fs_path(&path, "%s/%s", _rootDir, LFS_DIR_METADATA);
    success = success && cx_fs_mkdir(&path, _err);

    cx_fs_path(&path, "%s/%s", _rootDir, LFS_DIR_TABLES);
    success = success && cx_fs_mkdir(&path, _err);

    cx_fs_path(&path, "%s/%s", _rootDir, LFS_DIR_BLOCKS);
    success = success && cx_fs_mkdir(&path, _err);

    // create metadata file
    if (success)
    {
        success = false;

        cx_fs_path(&path, "%s/%s/%s", _rootDir, LFS_DIR_METADATA, LFS_DIR_METADATA);
        cx_fs_touch(&path, _err);

        t_config* meta = config_create(path);
        if (NULL != meta)
        {
            cx_str_from_uint32(_maxBlocks, temp, sizeof(temp));
            config_set_value(meta, "blocksCount", temp);

            cx_str_from_uint32(_blockSize, temp, sizeof(temp));
            config_set_value(meta, "blocksSize", temp);

            config_set_value(meta, "magicNumber", "LFS"); //TODO. ????

            config_save(meta);
            config_destroy(meta);

            success = true;
        }
        else
        {
            CX_ERROR_SET(_err, LFS_ERR_INIT_FS_BOOTSTRAP,
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

        cx_fs_path(&path, "%s/%s/%s", _rootDir, LFS_DIR_METADATA, LFS_FILE_BITMAP);
        success = cx_fs_write(&path, emptyBuffer, bytesNeeded, _err);
        
        free(emptyBuffer);
    }

    return success;
}

static bool _fs_load_meta(cx_error_t* _err)
{
    CX_MEM_ZERO(*_err);

    char* temp = NULL;
    char* key = "";
    
    cx_path_t metadataPath;
    cx_fs_path(&metadataPath, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_METADATA, LFS_DIR_METADATA);

    t_config* meta = config_create(metadataPath);

    if (NULL != meta)
    {
        key = "blocksCount";
        if (config_has_property(meta, key))
        {
            m_fsCtx->meta.blocksCount = (uint32_t)config_get_int_value(meta, key);
        }
        else
        {
            goto key_missing;
        }

        key = "blocksSize";
        if (config_has_property(meta, key))
        {
            m_fsCtx->meta.blocksSize = (uint32_t)config_get_int_value(meta, key);
        }
        else
        {
            goto key_missing;
        }

        key = "magicNumber";
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
        return true;

    key_missing:
        CX_ERROR_SET(_err, LFS_ERR_INIT_FS_META, "key '%s' is missing in the filesystem metadata file '%s'.", key, metadataPath);
        config_destroy(meta);
    }
    else
    {
        CX_ERROR_SET(_err, LFS_ERR_INIT_FS_META, "filesystem metadata '%s' is missing or not readable.", metadataPath);
    }
    
    return false;
}

static bool _fs_load_tables(cx_error_t* _err)
{
    CX_MEM_ZERO(*_err);

    m_fsCtx->tablesMap = cx_cdict_init();
    if (NULL == m_fsCtx->tablesMap)
    {
        CX_ERROR_SET(_err, 1, "tablesMap concurrent dictionary creation failed.");
        return false;
    }

    cx_path_t tablesPath;
    cx_fs_path(&tablesPath, "%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES);
    // not really needed, but just in case the tables folder is deleted, we can recover
    cx_fs_mkdir(&tablesPath, _err);
    
    cx_fs_explorer_t* explorer = cx_fs_explorer_init(&tablesPath, _err);
    cx_path_t tableFolderPath;
    cx_path_t tableName;
    uint16_t tableHandle = INVALID_HANDLE;
    table_t* table = NULL;
    uint32_t count = 0;

    if (NULL != explorer)
    {
        while (cx_fs_explorer_next_folder(explorer, &tableFolderPath))
        {
            cx_fs_get_name(&tableFolderPath, false, &tableName);
            
            tableHandle = cx_handle_alloc(g_ctx.tablesHalloc);
            if (INVALID_HANDLE != tableHandle)
            {
                table = &(g_ctx.tables[tableHandle]);
                if (_fs_table_init(table, tableName, _err))
                {
                    cx_cdict_set(m_fsCtx->tablesMap, tableName, table);
                    count++;
                }
                else
                {
                    CX_WARN(CX_ALW, "Table '%s' skipped. %s", _err->desc);
                }
            }
            else
            {
                CX_WARN(CX_ALW, "Table '%s' skipped, we ran out of table handles!", _err->desc);
            }
        }

        cx_fs_explorer_destroy(explorer);

        CX_INFO("%d tables imported from the filesystem", count);
        return true;
    }

    CX_ERROR_SET(_err, LFS_ERR_INIT_FS_TABLES,
        "Tables directory '%s' is not accessible.", tablesPath);
    return false;
}

static bool _fs_load_blocks(cx_error_t* _err)
{
    cx_path_t bitmapPath;
    cx_fs_path(&bitmapPath, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_METADATA, LFS_FILE_BITMAP);

    int32_t size = (int32_t)_fs_calc_bitmap_size(m_fsCtx->meta.blocksCount);
    m_fsCtx->blocksMap = malloc(size);
    
    int32_t bytesRead = cx_fs_read(&bitmapPath, m_fsCtx->blocksMap, size, _err);
    if (size == bytesRead)
    {
        return true;
    }
    else if (-1 == size)
    {
        _err->code = LFS_ERR_INIT_FS_BITMAP;
    }
    else
    {
        CX_ERROR_SET(_err, LFS_ERR_INIT_FS_BITMAP, "the amount of bytes expected and the "
            "amount of actual bytes read from the bitmap file '%s' do not match. "
            "the file might be corrupt at this point.", bitmapPath);
    }

    free(m_fsCtx->blocksMap);
    m_fsCtx->blocksMap = NULL;
    return false;
}

static bool _fs_table_init(table_t* _table, const char* _tableName, cx_error_t* _err)
{
    CX_CHECK(strlen(_tableName) > 0, "invalid table name!");

    return true
        && fs_table_get_meta(_tableName, &_table->meta, _err)
        && memtable_init(_tableName, &_table->memtable, _err);
}

static bool _fs_file_read(cx_path_t* _filePath, fs_file_t* _outFile, cx_error_t* _err)
{
    char* key = NULL;
    bool keyMissing = false;
    t_config* file = NULL;
    CX_MEM_ZERO(*_err);

    if (cx_fs_exists(_filePath))
    {
        file = config_create(*_filePath);
        if (NULL != file)
        {
            key = "size";
            if (config_has_property(file, key))
            {
                _outFile->size = (uint32_t)config_get_int_value(file, key);
            }
            else
            {
                keyMissing = true;
                goto key_missing;
            }

            key = "blocksCount";
            if (config_has_property(file, key))
            {
                _outFile->blocksCount = (uint32_t)config_get_int_value(file, key);
            }
            else
            {
                keyMissing = true;
                goto key_missing;
            }

            key = "blocks";
            if (config_has_property(file, key))
            {
                char** blocks = config_get_array_value(file, key);

                uint32_t i = 0, j = 0;
                bool finished = (NULL == blocks[i]);
                while (!finished && j < sizeof(_outFile->blocks))
                {
                    cx_str_to_uint32(blocks[i], &(_outFile->blocks[j++]));
                    free(blocks[i++]);
                    finished = (NULL == blocks[i]);
                }
                free(blocks);

                CX_CHECK(finished, "there're still pending blocks to read! static buffer of %d elements is not enough!",
                    sizeof(_outFile->blocks));

                CX_CHECK(_outFile->blocksCount == j, "blocksCount (%d) and actual amount of blocks read from the array (%d) do not match!",
                    _outFile->blocksCount, j);
            }
            else
            {
                keyMissing = true;
                goto key_missing;
            }

        key_missing:
            if (keyMissing)
            {
                CX_ERROR_SET(_err, 1, "File '%s' is corrupt. Key '%s' is missing.", *_filePath, key);
            }
        }
        else
        {
            CX_ERROR_SET(_err, 1, "File '%s' could not be loaded!", *_filePath);
        }
    }
    else
    {
        CX_ERROR_SET(_err, 1, "File '%s' does not exist.", *_filePath);
    }

    if (NULL != file) config_destroy(file);
    return (LFS_ERR_NONE == _err->code);
}

static bool _fs_file_write(cx_path_t* _filePath, fs_file_t* _file, cx_error_t* _err)
{
    t_config* file = NULL;
    CX_MEM_ZERO(*_err);
    char temp[32];

    if (cx_fs_remove(_filePath, _err) && cx_fs_touch(_filePath, _err))
    {
        file = config_create(*_filePath);
        if (NULL != file)
        {
            cx_str_from_uint32(_file->size, temp, sizeof(temp));
            config_set_value(file, "size", temp);

            cx_str_from_uint32(_file->blocksCount, temp, sizeof(temp));
            config_set_value(file, "blocksCount", temp);

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

                config_set_value(file, "blocks", buff);

                config_save(file);
                free(buffPrev);
                free(buff);
            }
            else
            {
                config_set_value(file, "blocks", "[]");
            }
        }
        else
        {
            CX_ERROR_SET(_err, 1, "file '%s' could not be loaded!", *_filePath);
        }
    }

    if (NULL != file) config_destroy(file);
    return (LFS_ERR_NONE == _err->code);
}