#ifndef LFS_LFS_H_
#define LFS_LFS_H_

#include <ker/defines.h>
#include <cx/cx.h>
#include <cx/file.h>
#include <cx/net.h>
#include <cx/cdict.h>

#include <commons/config.h>
#include <commons/log.h>
#include <commons/collections/dictionary.h>
#include <commons/collections/queue.h>

typedef enum LFS_TIMER
{
    LFS_TIMER_DUMP = 0,
    LFS_TIMER_COMPACT
} LFS_TIMER;

typedef struct cfg_t
{
    t_config*           handle;                 // pointer to so-commons-lib config adt.
    char                listeningIp[16];        // ip address on which this LFS server will listen on.
    uint16_t            listeningPort;          // tcp port on which this LFS server will listen on.
    uint16_t            workers;                // number of worker threads to spawn to process requests.
    char                rootDir[PATH_MAX];      // initial root directory of our filesystem.
    uint32_t            blocksCount;            // maximum number of blocks available in our filesystem.
    uint32_t            blocksSize;             // size in bytes of each block in our filesystem.
    uint32_t            delay;                  // artificial delay in ms for each operation performed.
    uint16_t            valueSize;              // size in bytes of a value field in a table record.
    uint32_t            dumpInterval;           // interval in ms to perform memtable dumps.
} cfg_t;

typedef struct fs_meta_t
{
    uint32_t            blocksSize;             // size in bytes of each block in our filesystem.
    uint32_t            blocksCount;            // number of blocks in our filesystem.
    char                magicNumber[100];       // "Un string fijo con el valor 'lfs'" ????
} fs_meta_t;

typedef struct fs_file_t
{
    cx_path_t           path;                   // absolute file path to this file in our filesystem.
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
    cx_cdict_t*         tablesMap;              // container for indexing table_t entries by table name.
    pthread_mutex_t     mtxBlocks;              // mutex for syncing blocks alloc/free operations;
    bool                mtxBlocksInit;          // true if mtxBlocks was successfully initialized and therefore needs to be destroyed.
} fs_ctx_t;

typedef enum MEMTABLE_TYPE
{
    MEMTABLE_TYPE_MEM = 1,
    MEMTABLE_TYPE_DISK,
} MEMTABLE_TYPE;

typedef struct memtable_t
{
    MEMTABLE_TYPE       type;                   // memtable type depending on initialization. searches are performed differently on each type.
    table_name_t        name;                   // name of the table which this memtable belongs to.
    pthread_mutex_t     mtx;                    // mutex for syncing add/dump/find (if needed).
    bool                mtxInitialized;         // true if mtx was successfully initialized and therefore needs to be destroyed.
    table_record_t*     records;                // array for storing memtable entries.
    uint32_t            recordsCount;           // number of elements in our array.
    uint32_t            recordsCapacity;        // total capacity of our array.
    bool                recordsSorted;          // true if the records array is sorted and therefore supports binary searches.
} memtable_t;

typedef struct table_t
{
    uint16_t            handle;                 // handle of this table entry in the tables container (index).
    bool                inUse;                  // true if this handle has a table assigned to, false if it doesn't.
    table_meta_t        meta;                   // table metadata.
    memtable_t          memtable;               // memtable for this table.
    bool                deleted;                // true if this table is marked as deleted (pending to be removed).
    bool                compacting;             // true if this table is performing a compaction.
    bool                blocked;                // true if this table is marked as blocked (pending to be compacted).
    double              blockedStartTime;       // time counter value of when the table block started.
    uint16_t            timerHandle;            // handle to the timer created with the desired compaction interval for this table.
    t_queue*            blockedQueue;           // queue with tasks which are awaiting for this table to become unblocked.
    uint16_t            operations;             // number of operations being performed on this table (number of current jobs which depend on its availability).
    pthread_mutex_t     mtxOperations;          // mutex to protect operations/blocked/compacting variables when being accessed/modified by multiple threads.
} table_t;

typedef struct lfs_ctx_t
{
    cfg_t               cfg;                                        // lfs node configuration data.
    t_log*              log;                                        // pointer to so-commons-lib log adt.
    bool                isRunning;                                  // true if the server is running. false if it's shutting down.
    cx_net_ctx_sv_t*    sv;                                         // server context for serving API requests coming from MEM nodes.
    table_t             tables[MAX_TABLES];                         // container for storing tables.
    cx_handle_alloc_t*  tablesHalloc;                               // handle allocator for tables container.
    char                buffer[MAX_PACKET_LEN - MIN_PACKET_LEN];    // temporary pre-allocated buffer for building packets.
    uint16_t            timerDump;                                  // dump operation timer handle.
} lfs_ctx_t;

extern lfs_ctx_t        g_ctx;

#endif // LFS_LFS_
