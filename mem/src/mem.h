#ifndef MEM_H_
#define MEM_H_

#include <ker/defines.h>
#include <cx/cx.h>
#include <cx/file.h>
#include <cx/net.h>
#include <cx/cdict.h>
#include <cx/reslock.h>
#include <cx/list.h>

#include <commons/config.h>
#include <commons/collections/dictionary.h>
#include <commons/collections/queue.h>

#define MEM_CFG_PASSWORD        "password"
#define MEM_CFG_MEM_NUMBER      "memNumber"
#define MEM_CFG_WORKERS         "workers"
#define MEM_CFG_LISTENING_IP    "listeningIp"
#define MEM_CFG_LISTENING_PORT  "listeningPort"
#define MEM_CFG_LFS_IP          "lfsIp"
#define MEM_CFG_LFS_PORT        "lfsPort"
#define MEM_CFG_LFS_PASSWORD    "lfsPassword"
#define MEM_CFG_SEEDS_IP        "seedsIp"
#define MEM_CFG_SEEDS_PORT      "seedsPort"
#define MEM_CFG_DELAY_MEM       "delayMem"
#define MEM_CFG_DELAY_LFS       "delayLfs"
#define MEM_CFG_MEM_SIZE        "memSize"
#define MEM_CFG_INT_JOURNALING  "intervalJournaling"
#define MEM_CFG_INT_GOSSIPING   "intervalGossiping"

typedef enum MEM_TIMER
{
    MEM_TIMER_JOURNAL = 0,
    MEM_TIMER_GOSSIP = 1,
    MEM_TIMER_COUNT
} MEM_TIMER;

typedef struct cfg_t
{
    password_t          password;                   // password for authenticating KER nodes.
    uint16_t            memNumber;                  // memory node identifier.
    uint16_t            workers;                    // number of worker threads to spawn to process requests.
    ipv4_t              listeningIp;                // ip address on which this MEM server will listen on.
    uint16_t            listeningPort;              // tcp port on which this MEM server will listen on.
    ipv4_t              lfsIp;                      // ip address on which the LFS server will listen on.
    uint16_t            lfsPort;                    // tcp port on which the LFS server will listen on.
    password_t          lfsPassword;                // password to authenticate with the LFS server.
    uint8_t             seedsCount;                 // number memory nodes already known.
    ipv4_t              seedsIps[MAX_MEM_SEEDS];    // ip addresses of memory nodes already known.
    uint16_t            seedsPorts[MAX_MEM_SEEDS];  // ports of memory nodes already known.
    uint32_t            delayMem;                   // memory access delay in milliseconds.
    uint32_t            delayLfs;                   // filesystem access delay in milliseconds.
    uint32_t            memSize;                    // size in bytes of the main memory.
    uint16_t            valueSize;                  // size in bytes of each record's value field.
    uint32_t            intervalJournaling;         // interval in milliseconds to perform the journaling process.
    uint32_t            intervalGossiping;          // interval in milliseconds to perform the gossiping process.
} cfg_t;

typedef struct segment_t                            // table
{
    table_name_t        tableName;                  // name of the table stored in this segment.
    cx_cdict_t*         pages;                      // table of page_t implemented as a dictionary for faster lookups.
    cx_reslock_t        reslock;                    // resource lock to protect this table.
} segment_t;

typedef struct page_t                               // record
{
    uint16_t            frameHandle;                // frame number in which this page is stored.
    bool                modified;                   // whether it contains changes that need to be reflected in the LFS or not.
    pthread_rwlock_t    rwlock;                     // read-write lock object for protecting page data.
    segment_t*          parent;                     // parent segment that currently owns this page.
    cx_list_node_t*     node;                       // pointer to the node that contains this page in the LRU cache.
} page_t;

typedef struct mm_ctx_t
{
    char*               mainMem;                    // pre-allocated main memory buffer (main memory frames in which we'll load pages).
    uint16_t            valueSize;                  // size in bytes of each record's value field.
    uint32_t            pageSize;                   // size in bytes of each page contained in our main memory.
    uint32_t            pageMax;                    // maximum amount of pages that fit in our main memory.
    cx_cdict_t*         tablesMap;                  // table of segment_t implemented as a dictionary for faster lookups.
    bool                journaling;                 // true if this memory is performing a journaling.
    t_queue*            blockedQueue;               // queue with tasks which are awaiting for this memory to become unblocked.
    cx_reslock_t        reslock;                    // resource lock to protect this memory.
    cx_handle_alloc_t*  framesHalloc;               // pointer to frames handle allocator.
    pthread_mutex_t     framesMtx;                  // mutex for syncing allocation/deallocation of frames.
    cx_list_t*          framesLru;                  // linked list for storing LRU pages stored in frames.
} mm_ctx_t;

typedef struct mem_ctx_t
{
    cfg_t               cfg;                        // mem node configuration data.
    bool                cfgInitialized;             // true if the config was initialized (file path generated + full load).
    cx_path_t           cfgFilePath;                // configuration file path.
    uint16_t            cfgFswatchHandle;           // cx fswatch handle for reloading the config file.
    bool                isRunning;                  // true if the server is running. false if it's shutting down.
    cx_net_ctx_sv_t*    sv;                         // server context for serving API requests coming from KER nodes.
    cx_net_ctx_cl_t*    lfs;                        // client context for connecting to the LFS node.
    bool                lfsHandshaking;             // true if we're authenticating with the LFS node.
    bool                lfsAvail;                   // true if the LFS node is available to process our requests.
    payload_t           buff1;                      // temporary pre-allocated buffer for building packets.
    payload_t           buff2;                      // temporary pre-allocated buffer for building packets.
    uint16_t            timerJournal;               // cx timer handle for running the journaling operation.
    uint16_t            timerGossip;                // cx timer handle for running the gossiping operation.
    char*               shutdownReason;             // reason that caused this MEM node to exit.
} mem_ctx_t;

extern mem_ctx_t        g_ctx;

#endif // MEM_H_