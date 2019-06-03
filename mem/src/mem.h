#ifndef MEM_H_
#define MEM_H_

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
#include <commons/collections/list.h>

typedef enum MEM_TIMER
{
    MEM_TIMER_JOURNAL = 0,
} MEM_TIMER;

typedef struct cfg_t
{
    t_config*           handle;                     // pointer to so-commons-lib config adt.
    uint16_t            memNumber;                  // memory node identifier.
    uint16_t            workers;                    // number of worker threads to spawn to process requests.
    ipv4_t              listeningIp;                // ip address on which this MEM server will listen on.
    uint16_t            listeningPort;              // tcp port on which this MEM server will listen on.
    ipv4_t              lfsIp;                      // ip address on which the LFS server will listen on.
    uint16_t            lfsPort;                    // tcp port on which the LFS server will listen on.
    uint8_t             seedsCount;                 // number memory nodes already known.
    ipv4_t              seedsIps[MAX_MEM_SEEDS];    // ip addresses of memory nodes already known.
    uint16_t            seedsPorts[MAX_MEM_SEEDS];  // ports of memory nodes already known.
    uint32_t            delayMem;                   // memory access delay in milliseconds.
    uint32_t            delayLfs;                   // filesystem access delay in milliseconds.
    uint32_t            memSize;                    // size in bytes of the main memory.
    uint32_t            valueSize;                  // size in bytes of each record's value field.
    uint32_t            intervalJournaling;         // interval in milliseconds to perform the journaling process.
    uint32_t            intervalGossiping;          // interval in milliseconds to perform the gossiping process.
} cfg_t;

typedef struct segment_t
{
    table_name_t        tableName;                  // name of the table stored in this segment.
    cx_cdict_t*         pages;                      // table of pages implemented as a dictionary for faster lookups.
    cx_reslock_t        reslock;                    // resource lock to protect this table.
} segment_t;

typedef struct page_t
{
    uint16_t            handle;                     // handle to the pages container or "page number".
    bool                modified;                   // whether it contains changes that need to be reflected in the LFS or not.
    pthread_rwlock_t    rwlock;                     // read-write lock object for protecting page data.
    segment_t*          parent;                     // parent segment that currently owns this page.
} page_t;

typedef struct mm_ctx_t
{
    char*               mainMem;                    // pre-allocated main memory buffer.
    uint32_t            valueSize;                  // size in bytes of each record's value field.
    uint32_t            pageSize;                   // size in bytes of each page contained in our main memory.
    uint32_t            pageMax;                    // maximum amount of pages that fit in our main memory.
    cx_cdict_t*         tablesMap;                  // table of segments implemented as a dictionary for faster lookups.
    bool                journaling;                 // true if this memory is performing a journaling.
    t_queue*            blockedQueue;               // queue with tasks which are awaiting for this memory to become unblocked.
    cx_reslock_t        reslock;                    // resource lock to protect this memory.
    cx_handle_alloc_t*  pagesHalloc;                // pointer to pages handle allocator.
    pthread_mutex_t     pagesMtx;                   // mutex for syncing allocation/deallocation of pages.
    t_list*             pagesLru;                   // linked list for storing LRU pages.
} mm_ctx_t;

typedef struct mem_ctx_t
{
    cfg_t               cfg;                        // mem node configuration data.
    t_log*              log;                        // pointer to so-commons-lib log adt.
    bool                isRunning;                  // true if the server is running. false if it's shutting down.
    cx_net_ctx_sv_t*    sv;                         // server context for serving API requests coming from KER nodes.
    cx_net_ctx_cl_t*    lfs;                        // client context for connecting to the LFS node.
    payload_t           buff1;                      // temporary pre-allocated buffer for building packets.
    payload_t           buff2;                      // temporary pre-allocated buffer for building packets.
} mem_ctx_t;

extern mem_ctx_t        g_ctx;

#endif // MEM_H_