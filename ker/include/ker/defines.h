#ifndef DEFINES_H_
#define DEFINES_H_

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <cx/cx.h>

#define TABLE_NAME_LEN_MIN 1
#define TABLE_NAME_LEN_MAX NAME_MAX

#define MEMTABLE_INITIAL_CAPACITY 256
#define MAX_TASKS 4096
#define MAX_TABLES 4096
#define MAX_FILE_FRAG 1024

typedef char table_name_t[TABLE_NAME_LEN_MAX + 1];

typedef enum CONSISTENCY_TYPE
{
    CONSISTENCY_STRONG = 1,
    CONSISTENCY_STRONG_HASH = 2,
    CONSISTENCY_EVENTUAL = 3,
} CONSISTENCY_TYPE;

static const char *CONSISTENCY_NAME[] = {
    "", "STRONG", "STRONG-HASH", "EVENTUAL"
};

typedef enum TASK_ORIGIN
{
    TASK_ORIGIN_NONE = 0,           // default, unasigned state.
    TASK_ORIGIN_CLI,                // the origin of this task is the command line interface.
    TASK_ORIGIN_API,                // the origin of this task is a client connected to our server.
    TASK_ORIGIN_INTERNAL,           // the origin of this task is either a timer, our main thread or a worker thread.
} TASK_ORIGIN;

typedef enum TASK_STATE
{
    TASK_STATE_NONE = 0,            // default, unasigned state.
    TASK_STATE_NEW,                 // the task was just created, but it's not yet assigned to the primary queue.
    TASK_STATE_READY,               // the task is ready to be executed and waiting in the queue.
    TASK_STATE_RUNNING,             // the task is being processed by a worker thread.
    TASK_STATE_COMPLETED,           // the task is completed. check c->err to handle errors (if any).
    TASK_STATE_BLOCKED_RESCHEDULE,  // the task cannot be performed at this time since the table is blocked and it must be re-rescheduled.
    TASK_STATE_BLOCKED_AWAITING,    // the task is awaiting in a table's blocked queue. as soon as the table is unblocked it will be moved to the main queue.
} TASK_STATE;

typedef enum TASK_TYPE
{  
    TASK_TYPE_NONE = 0,             // default, unasigned state.
    TASK_MT = UINT8_C(0x0),         // main thread task bitflag.
    TASK_WT = UINT8_C(0x80),        // worker thread task bitflag.
    // Main-thread tasks ----------------------------------------------------------------
    TASK_MT_FREE        = TASK_MT | UINT8_C(01), // main thread task to free a table when it's no longer in use.
    TASK_MT_COMPACT     = TASK_MT | UINT8_C(02), // main thread task to compact a table when it's no longer in use.
    TASK_MT_DUMP        = TASK_MT | UINT8_C(03), // main thread task to dump all the existing tables.
    TASK_MT_UNBLOCK     = TASK_MT | UINT8_C(04), // main thread task to unblock a table.
    // Worker-thread tasks --------------------------------------------------------------
    TASK_WT_CREATE      = TASK_WT | UINT8_C(01), // worker thread task to create a table.
    TASK_WT_DROP        = TASK_WT | UINT8_C(02), // worker thread task to drop a table.
    TASK_WT_DESCRIBE    = TASK_WT | UINT8_C(03), // worker thread task to describe single/multiple table/s.
    TASK_WT_SELECT      = TASK_WT | UINT8_C(04), // worker thread task to do a select query on a table.
    TASK_WT_INSERT      = TASK_WT | UINT8_C(05), // worker thread task to do a single/bulk insert query in a table.
    TASK_WT_DUMP        = TASK_WT | UINT8_C(06), // worker thread task to dump a table.
    TASK_WT_COMPACT     = TASK_WT | UINT8_C(07), // worker thread task to compact a table.
} TASK_TYPE;

typedef struct table_meta_t
{
    table_name_t            name;                           // name of the table.
    CONSISTENCY_TYPE        consistency;                    // constistency needed for this table.
    uint16_t                partitionsCount;                // number of partitions for this table.
    uint32_t                compactionInterval;             // interval in ms to perform table compaction.
} table_meta_t;

typedef struct table_record_t
{
    uint16_t            key;
    uint32_t            timestamp;
    char*               value;
} table_record_t;

typedef struct data_common_t
{
    cx_err_t        err;
    double          startTime;
    uint16_t        remoteId;
    uint16_t        tableHandle;
} data_common_t;

typedef struct data_create_t
{
    data_common_t   c;
    table_name_t    tableName;
    uint8_t         consistency;
    uint16_t        numPartitions;
    uint32_t        compactionInterval;
} data_create_t;

typedef struct data_drop_t
{
    data_common_t   c;
    table_name_t    tableName;
} data_drop_t;

typedef struct data_describe_t
{
    data_common_t   c;
    table_meta_t*   tables;
    uint16_t        tablesCount;
} data_describe_t;

typedef struct data_select_t
{
    data_common_t   c;
    table_name_t    tableName;
    table_record_t  record;
} data_select_t;

typedef struct data_insert_t
{
    data_common_t   c;
    table_name_t    tableName;
    table_record_t  record;
} data_insert_t;

typedef struct data_dump_t
{
    data_common_t   c;
    table_name_t    tableName;
} data_dump_t;

typedef struct data_compact_t
{
    data_common_t   c;
    table_name_t    tableName;
} data_compact_t;

typedef struct data_run_t
{
    data_common_t   c;
} data_run_t;

typedef struct data_add_memory_t
{
    data_common_t   c;
} data_add_memory_t;

#endif // DEFINES_H_

