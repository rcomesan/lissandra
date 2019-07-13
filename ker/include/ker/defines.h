#ifndef DEFINES_H_
#define DEFINES_H_

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <cx/cx.h>
#include <cx/net.h>
#include <cx/linesf.h>

#define TABLE_NAME_LEN_MIN 1
#define TABLE_NAME_LEN_MAX NAME_MAX

#define MEMTABLE_INITIAL_CAPACITY 256

#define MAX_TASKS 4096
#define MAX_TABLES 4096

#define MAX_FILE_FRAG 1024

#define MAX_MEM_SEEDS 16
#define MAX_MEM_NODES 100
#define INVALID_MEM_NUMBER 0

#define MIN_PASSWD_LEN 12
#define MAX_PASSWD_LEN 32

typedef char payload_t[MAX_PACKET_LEN - MIN_PACKET_LEN];
typedef char table_name_t[TABLE_NAME_LEN_MAX + 1];
typedef char password_t[MAX_PASSWD_LEN + 1];

typedef enum ERR_CODE
{
#ifndef ERR_NONE
    ERR_NONE = 0,
#endif
    ERR_GENERIC = 1,
    ERR_LOGGER_FAILED,
    ERR_INIT_MTX,
    ERR_INIT_RWL,
    ERR_INIT_HALLOC,
    ERR_INIT_THREADPOOL,
    ERR_INIT_QUEUE,
    ERR_INIT_LIST,
    ERR_INIT_CDICT,
    ERR_INIT_RESLOCK,
    ERR_INIT_TIMER,
    ERR_INIT_NET,
    ERR_INIT_FS_ROOTDIR,
    ERR_INIT_FS_BOOTSTRAP,
    ERR_INIT_FS_META,
    ERR_INIT_FS_TABLES,
    ERR_INIT_FS_BITMAP,
    ERR_INIT_MM_MAIN,
    ERR_INIT_MM_PAGES,
    ERR_CFG_NOTFOUND,
    ERR_CFG_MISSINGKEY,
    ERR_NET_LFS_UNAVAILABLE,
    ERR_NET_MEM_UNAVAILABLE,
    ERR_TABLE_BLOCKED,
    ERR_MEMORY_BLOCKED,
    ERR_MEMORY_FULL,
    ERR_QUANTUM_EXHAUSTED
} ERR_CODE;


#define RESOURCE_TYPE_TABLE 1

typedef enum QUERY_TYPE
{
    QUERY_NONE = 0,
    QUERY_CREATE,
    QUERY_DROP,
    QUERY_DESCRIBE,
    QUERY_SELECT,
    QUERY_INSERT,
    QUERY_JOURNAL,
    QUERY_ADDMEMORY,
    QUERY_RUN,
    QUERY_LOGFILE,
    QUERY_EXIT,
    QUERY_COUNT
} QUERY_TYPE;

static const char *QUERY_NAME[] = {
    "NONE", "CREATE", "DROP", "DESCRIBE", "SELECT", "INSERT",
    "JOURNAL", "ADD MEMORY", "RUN", "LOGFILE", "EXIT",
};

typedef enum CONSISTENCY_TYPE
{
    CONSISTENCY_NONE = 0,
    CONSISTENCY_STRONG,
    CONSISTENCY_STRONG_HASHED,
    CONSISTENCY_EVENTUAL,
    CONSISTENCY_COUNT
} CONSISTENCY_TYPE;

static const char *CONSISTENCY_NAME[] = {
    "INDISTINCT", "STRONG", "STRONG-HASHED", "EVENTUAL"
};

typedef struct table_meta_t
{
    table_name_t    name;                           // name of the table.
    uint8_t         consistency;                    // constistency needed for this table.
    uint16_t        partitionsCount;                // number of partitions for this table.
    uint32_t        compactionInterval;             // interval in ms to perform table compaction.
} table_meta_t;

typedef struct table_record_t
{
    uint16_t        key;
    uint64_t        timestamp;
    char*           value;
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
    uint16_t        tablesRemaining;
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

typedef struct data_free_t
{
    uint8_t         resourceType;
    void*           resourcePtr;
} data_free_t;

typedef struct data_run_t
{
    cx_path_t       scriptFilePath;
    cx_linesf_t*    script;
    cx_path_t       outputFilePath;
    FILE*           output;
    uint32_t        lineNumber;
} data_run_t;

typedef struct data_addmem_t
{
    uint16_t        memNumber;
    uint8_t         consistency;
} data_addmem_t;

#endif // DEFINES_H_
