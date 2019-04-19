#include "fs.h"

#include <cx/mem.h>
#include <cx/fs.h>
#include <cx/str.h>

#include <commons/config.h>

#include <string.h>

static fs_ctx_t*       m_fsCtx = NULL;        // private filesystem context

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool         _fs_is_lfs(cx_path_t* _rootDir);

static uint32_t     _fs_calc_bitmap_size(uint32_t _maxBlocks);

static bool         _fs_bootstrap(cx_path_t* _rootDir, uint32_t _maxBlocks, uint32_t _blockSize, cx_error_t* _err);

static bool         _fs_load_meta(cx_error_t* _err);

static bool         _fs_load_tables(cx_error_t* _err);

static bool         _fs_load_table_meta(cx_path_t* _tableFolderPath, table_meta_t* _outMeta, cx_error_t* _err);

static bool         _fs_load_blocks(cx_error_t* _err);

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
        
        return true
            && _fs_load_meta(_err)
            && _fs_load_tables(_err)
            && _fs_load_blocks(_err);
    }

    return false;
}

void fs_destroy()
{
    if (NULL == m_fsCtx) return;

    // destroy blocksmap and his underlying buffer
    if (NULL != m_fsCtx->blocksmap)
    {
        bitarray_destroy(m_fsCtx->blocksmap);
        m_fsCtx->blocksmap = NULL;
    }
    free(m_fsCtx->blocksmapBuffer);
    m_fsCtx->blocksmapBuffer = NULL;
    
    // destroy tables
    char* key;
    table_t* table;

    cx_cdict_iter_begin(g_ctx.tables);
    while (cx_cdict_iter_next(g_ctx.tables, &key, (void**)&table))
    {
        //TODO destroy memtable here
        table->memtable = NULL;
        free(table);
    }
    cx_cdict_iter_end(g_ctx.tables);
    cx_cdict_clear(g_ctx.tables, NULL);

    free(m_fsCtx);
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

    CX_INFO("bootstrapping lissandra filesystem in '%s'...", _rootDir);
    
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

    uint32_t count = 0;

    cx_path_t tablesPath;
    cx_fs_path(&tablesPath, "%s/%s", m_fsCtx->rootDir, LFS_DIR_TABLES);

    // not really needed, but just in case the tables folder is deleted, we can recover
    cx_fs_mkdir(&tablesPath, _err);

    cx_fs_explorer_t* explorer = cx_fs_explorer_init(&tablesPath, _err);
    cx_path_t tableFolderPath;
    cx_path_t tableName;
    table_t* table = NULL;

    if (NULL != explorer)
    {
        while (cx_fs_explorer_next_folder(explorer, &tableFolderPath))
        {
            table = CX_MEM_STRUCT_ALLOC(table);

            if (_fs_load_table_meta(&tableFolderPath, &table->meta, _err))
            {
                // initialize stuff
                table->memtable = NULL; //TODO initialize memtable here

                cx_cdict_set(g_ctx.tables, tableName, table);
                count++;
            }
            else
            {
                CX_WARN(CX_ALW, "table '%s' skipped. %s", _err->desc);
            }
        }

        cx_fs_explorer_destroy(explorer);

        CX_INFO("%d tables imported from the filesystem", count);
        return true;
    }

    CX_ERROR_SET(_err, LFS_ERR_INIT_FS_TABLES,
        "tables directory '%s' is not accessible.", tablesPath);
    return false;
}

static bool _fs_load_table_meta(cx_path_t* _tableFolderPath, table_meta_t* _outMeta, cx_error_t* _err)
{
    CX_MEM_ZERO(*_err);
    CX_CHECK_NOT_NULL(_outMeta);

    char* key = "";

    cx_path_t metadataPath;
    cx_fs_path(&metadataPath, "%s/%s", *_tableFolderPath, LFS_DIR_METADATA);
    
    cx_path_t tableName;
    cx_fs_get_name(_tableFolderPath, false, &tableName);
    cx_str_copy(_outMeta->name, sizeof(_outMeta->name), tableName);

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

        return true;

    key_missing:
        CX_ERROR_SET(_err, 1, "key '%s' is missing in table '%s' metadata file.", key, tableName);
    }

    CX_ERROR_SET(_err, 1, "table metadata file '%s' is missing or not readable.", metadataPath);
    return false;
}

static bool _fs_load_blocks(cx_error_t* _err)
{
    cx_path_t bitmapPath;
    cx_fs_path(&bitmapPath, "%s/%s/%s", m_fsCtx->rootDir, LFS_DIR_METADATA, LFS_FILE_BITMAP);

    int32_t size = (int32_t)_fs_calc_bitmap_size(m_fsCtx->meta.blocksCount);
    m_fsCtx->blocksmapBuffer = malloc(size);
    
    int32_t bytesRead = cx_fs_read(&bitmapPath, m_fsCtx->blocksmapBuffer, size, _err);
    if (size == bytesRead)
    {
        m_fsCtx->blocksmap = bitarray_create_with_mode(m_fsCtx->blocksmapBuffer, size, MSB_FIRST);
        CX_CHECK(NULL != m_fsCtx->blocksmap, "bitarray creation failed!");
        return NULL != m_fsCtx->blocksmap;
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

    free(m_fsCtx->blocksmapBuffer);
    m_fsCtx->blocksmapBuffer = NULL;
    return false;
}