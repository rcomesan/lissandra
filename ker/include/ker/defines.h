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

typedef struct data_create_t
{
    table_name_t    tableName;
    uint8_t         consistency;
    uint16_t        numPartitions;
    uint32_t        compactionInterval;
} data_create_t;

typedef struct data_drop_t
{
    table_name_t    tableName;
} data_drop_t;

typedef struct data_describe_t
{
    table_meta_t*   tables;
    uint16_t        tablesCount;
} data_describe_t;

typedef struct data_select_t
{
    table_name_t    tableName;
    table_record_t  record;
} data_select_t;

typedef struct data_insert_t
{
    table_name_t    tableName;
    table_record_t  record;
} data_insert_t;

typedef struct data_dump_t
{
    table_name_t    tableName;
} data_dump_t;

typedef struct data_compact_t
{
    table_name_t    tableName;
} data_compact_t;

typedef struct data_run_t
{
    int32_t         foo;
} data_run_t;

typedef struct data_add_memory_t
{
    int32_t         bar;
} data_add_memory_t;

#endif // DEFINES_H_

