#ifndef DEFINES_H_
#define DEFINES_H_

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#define TABLE_NAME_LEN_MIN 1
#define TABLE_NAME_LEN_MAX NAME_MAX

#define MEMTABLE_INITIAL_CAPACITY 256

#define MAX_CONCURRENT_REQUESTS 4096

typedef enum CONSISTENCY_TYPE
{
    CONSISTENCY_STRONG = 1,
    CONSISTENCY_STRONG_HASH = 2,
    CONSISTENCY_EVENTUAL = 3,
} CONSISTENCY_TYPE;

static const char *CONSISTENCY_NAME[] = {
    "", "STRONG", "STRONG-HASH", "EVENTUAL"
};

typedef enum REQ_ORIGIN
{
    REQ_ORIGIN_CLI = 1,
    REQ_ORIGIN_API = 2
} REQ_ORIGIN;

typedef enum REQ_TYPE
{
    REQ_TYPE_NONE = 0,
    REQ_TYPE_CREATE,
    REQ_TYPE_DROP,
    REQ_TYPE_DESCRIBE,
    REQ_TYPE_SELECT,
    REQ_TYPE_INSERT,
} REQ_TYPE;

typedef enum REQ_STATE
{
    REQ_STATE_NONE = 0,
    REQ_STATE_NEW,
    REQ_STATE_READY,
    REQ_STATE_RUNNING,
    REQ_STATE_COMPLETED,
} REQ_STATE;

typedef struct table_meta_t
{
    char                    name[TABLE_NAME_LEN_MAX + 1];   // name of the table.
    CONSISTENCY_TYPE        consistency;                    // constistency needed for this table.
    uint16_t                partitionsCount;                // number of partitions for this table.
    uint32_t                compactionInterval;             // interval in ms to perform table compaction.
} table_meta_t;

typedef struct data_common_t
{
    bool            success;
    double          startTime;
    uint16_t        remoteId;
} data_common_t;

typedef struct data_create_t
{
    data_common_t   c;
    char            tableName[TABLE_NAME_LEN_MAX + 1];
    uint8_t         consistency;
    uint16_t        numPartitions;
    uint32_t        compactionInterval;
} data_create_t;

typedef struct data_drop_t
{
    data_common_t   c;
    char            tableName[TABLE_NAME_LEN_MAX + 1];
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
    char            tableName[TABLE_NAME_LEN_MAX + 1];
    uint16_t        key;
    char*           value;
} data_select_t;

typedef struct data_insert_t
{
    data_common_t   c;
    char            tableName[TABLE_NAME_LEN_MAX +1 ];
    uint16_t        key;
    char*           value;
    uint32_t        timestamp;
} data_insert_t;

typedef struct data_run_t
{
    data_common_t   c;
} data_run_t;

typedef struct data_add_memory_t
{
    data_common_t   c;
} data_add_memory_t;

#endif // DEFINES_H_
