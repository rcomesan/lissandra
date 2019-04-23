#ifndef LFS_FS_H_
#define LFS_FS_H_

#include "lfs.h"

#include <stdint.h>
#include <stdbool.h>

#include <cx/cx.h>

#include <pthread.h>

#define LFS_ROOT_FILE_MARKER ".lfs_root"
#define LFS_DIR_METADATA "metadata"
#define LFS_DIR_TABLES "tables"
#define LFS_DIR_BLOCKS "blocks"
#define LFS_FILE_BITMAP "bitmap.bin"

typedef struct fs_meta_t
{
    uint32_t            blocksSize;             // size in bytes of each block in our filesystem.
    uint32_t            blocksCount;            // number of blocks in our filesystem.
    char                magicNumber[100];       // "Un string fijo con el valor 'lfs'" ????
} fs_meta_t;

typedef struct fs_file_t
{
    uint32_t            size;                   // size in bytes of the file stored in the fs.
    uint32_t            blocks[MAX_FILE_FRAG];  // ordered array containing the number of each block that stores bytes of our partitioned file.
    uint32_t            blocksCount;            // number of elements in the blocks array.
} fs_file_t;

typedef struct fs_ctx_t
{
    fs_meta_t           meta;                   // filesystem metadata.
    char                rootDir[PATH_MAX];      // initial root directory of our filesystem.
    char*               blocksMap;              // buffer for storing our bit array containing blocks status (unset bit mean the block is free to use).
                                                // must be large enough to hold at least meta.blocksCount amount of bits.
   
    pthread_mutex_t     mtxCreateDrop;          // mutex for syncing create/drop queries.
    bool                mtxCreateDropInit;      // true if mtxCreateDrop was successfully initialized and therefore needs to be destroyed.
    pthread_mutex_t     mtxBlocks;              // mutex for syncing blocks alloc/free operations;
    bool                mtxBlocksInit;          // true if mtxBlocks was successfully initialized and therefore needs to be destroyed.
} fs_ctx_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool                fs_init(cx_error_t* _err);

void                fs_destroy();

table_meta_t*       fs_describe(uint16_t* _outTablesCount, cx_error_t* _err);

bool                fs_table_exists(const char* _tableName);

bool                fs_table_is_blocked(const char* _tableName);

bool                fs_table_create(const char* _tableName, uint8_t _consistency, uint16_t _partitions, uint32_t _compactionInterval, cx_error_t* _err);

bool                fs_table_delete(const char* _tableName, cx_error_t* _err);

bool                fs_table_get_meta(const char* _tableName, table_meta_t* _outMeta, cx_error_t* _err);

bool                fs_table_get_part(const char* _tableName, uint16_t _partNumber, fs_file_t* _outFile, cx_error_t* _err);

bool                fs_table_set_part(const char* _tableName, uint16_t _partNumber, fs_file_t* _inFile, cx_error_t* _err);

void                fs_table_get_dump(const char* _tableName, uint16_t _dumpNumber, fs_file_t* _outFile);

uint32_t            fs_block_alloc(uint32_t _blocksCount, uint32_t* _outBlocksArr);

void                fs_block_free(uint32_t* _blocksArr, uint32_t _blocksCount);

int32_t             fs_block_read(uint32_t _blockNumber, char* _buffer, cx_error_t* _err);

bool                fs_block_write(uint32_t _blockNumber, char* _buffer, uint32_t _bufferSize, cx_error_t* _err);

#endif // LFS_FS_H_
