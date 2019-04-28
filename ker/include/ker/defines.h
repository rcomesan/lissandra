#ifndef DEFINES_H_
#define DEFINES_H_

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <cx/cx.h>

#define TABLE_NAME_LEN_MIN 1
#define TABLE_NAME_LEN_MAX NAME_MAX

#define MEMTABLE_INITIAL_CAPACITY 256
#define MAX_CONCURRENT_REQUESTS 4096
#define MAX_TABLES 4096
#define MAX_FILE_FRAG 1024

#define ERR_NONE 0

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
    REQ_STATE_BLOCKED_DOAGAIN,
} REQ_STATE;

typedef char table_name_t[TABLE_NAME_LEN_MAX + 1];

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
    cx_error_t      err;
    double          startTime;
    uint16_t        remoteId;
} data_common_t;

typedef struct data_create_t
{
    data_common_t   c;
    table_name_t    name;
    uint8_t         consistency;
    uint16_t        numPartitions;
    uint32_t        compactionInterval;
    uint16_t        tableHandle;
} data_create_t;

typedef struct data_drop_t
{
    data_common_t   c;
    table_name_t    name;
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
    table_name_t    name;
    table_record_t  record;
} data_select_t;

typedef struct data_insert_t
{
    data_common_t   c;
    table_name_t    name;
    table_record_t  record;
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

