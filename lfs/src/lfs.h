#ifndef LFS_LFS_H_
#define LFS_LFS_H_

#include <ker/defines.h>
#include <cx/cx.h>
#include <cx/file.h>
#include <cx/net.h>
#include <cx/cdict.h>
#include <cx/reslock.h>

#include <commons/config.h>
#include <commons/log.h>
#include <commons/collections/dictionary.h>
#include <commons/collections/queue.h>

#define LFS_CFG_PASSWORD                "password"
#define LFS_CFG_LISTENING_IP            "listeningIp"
#define LFS_CFG_LISTENING_PORT          "listeningPort"
#define LFS_CFG_WORKERS                 "workers"
#define LFS_CFG_ROOT_DIR                "rootDirectory"
#define LFS_CFG_BLOCKS_COUNT            "blocksCount"
#define LFS_CFG_BLOCKS_SIZE             "blocksSize"
#define LFS_CFG_DELAY                   "delay"
#define LFS_CFG_VALUE_SIZE              "valueSize"
#define LFS_CFG_INT_DUMP                "dumpInterval"

#define LFS_ROOT_FILE_MARKER            ".lfs_root"
#define LFS_MAGIC_NUMBER                "LISSANDRA"

#define LFS_DIR_METADATA                "Metadata"
#define LFS_DIR_TABLES                  "Tables"
#define LFS_DIR_BLOCKS                  "Bloques"

#define LFS_FILE_METADATA               "Metadata.bin"
#define LFS_FILE_BITMAP                 "Bitmap.bin"

#define LFS_PART_PREFIX                 "P"
#define LFS_PART_EXTENSION              "bin"
#define LFS_PART_EXTENSION_COMPACTION   "binc"

#define LFS_DUMP_PREFIX                 "D"
#define LFS_DUMP_EXTENSION              "tmp"
#define LFS_DUMP_EXTENSION_COMPACTION   "tmpc"

#define LFS_BLOCK_PREFIX                ""
#define LFS_BLOCK_EXTENSION             "bin"

#define LFS_META_PROP_BLOCKS_COUNT      "BLOCKS"
#define LFS_META_PROP_BLOCKS_SIZE       "BLOCK_SIZE"
#define LFS_META_PROP_MAGIC_NUMBER      "MAGIC_NUMBER"

#define LFS_FILE_PROP_BLOCKS            "BLOCKS"
#define LFS_FILE_PROP_SIZE              "SIZE"

#define LFS_DELIM_VALUE                 ";"
#define LFS_DELIM_RECORD                "\n"

typedef enum LFS_TIMER
{
    LFS_TIMER_DUMP = 0,
    LFS_TIMER_COMPACT,
    LFS_TIMER_COUNT
} LFS_TIMER;

typedef struct cfg_t
{
    t_config*           handle;                 // pointer to so-commons-lib config adt.
    password_t          password;               // password for authenticating MEM nodes.
    ipv4_t              listeningIp;            // ip address on which this LFS server will listen on.
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
    char                magicNumber[100];       // a constant text value used to identify a file format (LISSANDRA).
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
    MEMTABLE_TYPE_NONE = 0,
    MEMTABLE_TYPE_MEM,                          // default memtable.
    MEMTABLE_TYPE_DISK,                         // memtable loaded from disk (either from a dump or a partition).
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
    table_meta_t        meta;                   // table metadata.
    memtable_t          memtable;               // memtable for this table.
    bool                compacting;             // true if this table is performing a compaction.
    t_queue*            blockedQueue;           // queue with tasks which are awaiting for this table to become unblocked.
    uint16_t            timerHandle;            // handle to the timer created with the desired compaction interval for this table.
    cx_reslock_t        reslock;                // resource lock to protect this table.
} table_t;

typedef struct lfs_ctx_t
{
    cfg_t               cfg;                    // lfs node configuration data.
    bool                isRunning;              // true if the server is running. false if it's shutting down.
    cx_net_ctx_sv_t*    sv;                     // server context for serving API requests coming from MEM nodes.
    payload_t           buff1;                  // temporary pre-allocated buffer for building packets.
    payload_t           buff2;                  // temporary pre-allocated buffer for building packets.
    uint16_t            timerDump;              // dump operation timer handle.
    char*               shutdownReason;         // reason that caused this MEM node to exit.
} lfs_ctx_t;

extern lfs_ctx_t        g_ctx;

#endif // LFS_LFS_
