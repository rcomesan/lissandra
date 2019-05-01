#ifndef LFS_FS_H_
#define LFS_FS_H_

#include "lfs.h"

#include <stdint.h>
#include <stdbool.h>

#include <cx/cx.h>
#include <cx/fs.h>

#include <pthread.h>

#define LFS_ROOT_FILE_MARKER ".lfs_root"
#define LFS_DIR_METADATA "metadata"
#define LFS_DIR_TABLES "tables"
#define LFS_DIR_BLOCKS "blocks"
#define LFS_FILE_BITMAP "bitmap.bin"
#define LFS_PART_PREFIX "P"
#define LFS_PART_EXTENSION "bin"
#define LFS_PART_EXTENSION_COMPACTION "binc"
#define LFS_DUMP_EXTENSION "tmp"
#define LFS_DUMP_EXTENSION_COMPACTION "tmpc"
#define LFS_DUMP_PREFIX "D"

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool                fs_init(cx_err_t* _err);

void                fs_destroy();

table_meta_t*       fs_describe(uint16_t* _outTablesCount, cx_err_t* _err);

bool                fs_table_init(table_t* _outTable, const char* _tableName, cx_err_t* _err);

void                fs_table_destroy(table_t* _table);

bool                fs_table_exists(const char* _tableName, table_t** _outTable);

bool                fs_table_avail_guard_begin(table_t* _table, cx_err_t* _err, pthread_mutex_t* _mtx);

void                fs_table_avail_guard_end(table_t* _table);

bool                fs_table_create(table_t* _table, const char* _tableName, uint8_t _consistency, uint16_t _partitions, uint32_t _compactionInterval, cx_err_t* _err);

bool                fs_table_delete(const char* _tableName, cx_err_t* _err);

bool                fs_table_get_meta(const char* _tableName, table_meta_t* _outMeta, cx_err_t* _err);

bool                fs_table_get_part(const char* _tableName, uint16_t _partNumber, bool _isDuringCompaction, fs_file_t* _outFile, cx_err_t* _err);

bool                fs_table_set_part(const char* _tableName, uint16_t _partNumber, bool _isDuringCompaction, fs_file_t* _file, cx_err_t* _err);

bool                fs_table_get_dump(const char* _tableName, uint16_t _dumpNumber, bool _isDuringCompaction, fs_file_t* _outFile, cx_err_t* _err);

bool                fs_table_set_dump(const char* _tableName, uint16_t _dumpNumber, bool _isDuringCompaction, fs_file_t* _file, cx_err_t* _err);

uint16_t            fs_table_get_dump_number_next(const char* _tableName);

cx_fs_explorer_t*   fs_table_explorer(const char* _tableName, cx_err_t* _err);

uint32_t            fs_block_alloc(uint32_t _blocksCount, uint32_t* _outBlocksArr);

void                fs_block_free(uint32_t* _blocksArr, uint32_t _blocksCount);

int32_t             fs_block_read(uint32_t _blockNumber, char* _buffer, cx_err_t* _err);

bool                fs_block_write(uint32_t _blockNumber, char* _buffer, uint32_t _bufferSize, cx_err_t* _err);

uint32_t            fs_block_size();

bool                fs_file_load(fs_file_t* _file, char* _buffer, cx_err_t* _err);

bool                fs_is_dump(cx_path_t* _filePath, uint16_t* _outDumpNumber, bool* _outDuringCompaction);

#endif // LFS_FS_H_
