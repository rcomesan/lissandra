#ifndef LFS_H_
#define LFS_H_

#include <commons/collections/dictionary.h>
#include <cx/net.h>
#include <limits.h>

#define TABLE_NAME_LEN_MIN 1
#define TABLE_NAME_LEN_MAX NAME_MAX
#define MEMTABLE_INITIAL_CAPACITY 256

typedef struct cfg_t
{
    char                    listeningIp[16];    // ip address on which the LFS server will listen on
    uint16_t                listeningPort;      // tcp port on which the LFS server will listen on
    char                    rootDir[PATH_MAX];  // initial root directory of our filesystem
    uint32_t                delay;              // artificial delay in ms for each operation performed
    uint16_t                valueSize;          // size in bytes of a value field in a table record
    uint32_t                dumpInterval;       // interval in ms to perform memtable dumps
} cfg_t;

typedef struct table_meta_t
{
    char                    name[TABLE_NAME_LEN_MAX + 1];   // name of the table
    uint8_t                 consistency;                    // constistency needed for this table
    uint16_t                partitionsCount;                // number of partitions for this table
    uint32_t                compactionInterval;             // interval in ms to perform table compaction
} table_meta_t;

extern cx_net_ctx_sv_t*     g_sv;               // server context for serving API requests coming from MEM nodes
extern char                 g_buffer[MAX_PACKET_LEN - MIN_PACKET_LEN];  // temporary pre-allocated buffer for building packets
extern t_dictionary*        g_memtable;         // memtables container indexed by table name

#endif // LFS_H_
