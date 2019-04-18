#ifndef FS_H_
#define FS_H_

#include "lfs.h"

#include <stdint.h>
#include <stdbool.h>

#include <commons/bitarray.h>

typedef struct fs_meta_t
{
    uint32_t        blockSize;          // size in bytes of each block in our filesystem
    uint32_t        blocksCount;        // number of blocks in our filesystem
    char*           magicNumber;        // "Un string fijo con el valor 'lfs'" ????
} fs_meta_t;

typedef struct fs_file_t
{
    uint32_t        size;               // size in bytes of the file stored in the fs
    uint32_t*       blocks;             // ordered array containing the number of each block that stores bytes of our partitioned file
    uint32_t        blocksCount;        // number of elements in the blocks array
} fs_file_t;

typedef struct fs_ctx_t
{
    fs_meta_t       meta;               // filesystem metadata
    char            rootDir[PATH_MAX];  // initial root directory of our filesystem
    char*           blocksMapBuffer;    // buffer for storing our bit array containing blocks status (unset bit mean the block is free to use)
                                        // must be large enough to hold at least meta.blocksCount amount of bits
    t_bitarray*     blocksMap;          // bit array adt (so-commons-lib implementation)
    uint32_t        blockNumberLast;    // last block number allocated to resume contigous (if possible) allocation the next time alloc is called
} fs_ctx_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool                fs_init(const char* _rootFilePath);

void                fs_destroy();

bool                fs_table_exists(const char* _tableName);

void                fs_table_create(const char* _tableName, uint8_t _consistency, uint16_t _partitions);

void                fs_table_get_meta(const char* _tableName, table_meta_t* _outMeta);

void                fs_table_get_part(const char* _tableName, uint16_t _partNumber, fs_file_t* _outFile);

void                fs_table_get_dump(const char* _tableName, uint16_t _dumpNumber, fs_file_t* _outFile);

uint32_t            fs_block_alloc();

void                fs_block_free(uint32_t _blockNumber);

void                fs_block_read(uint32_t _blockNumber, char* _buffer, uint32_t _bufferSize);

void                fs_block_write(uint32_t _blockNumber, char* _buffer, uint32_t _bufferSize);

#endif // FS_H_
