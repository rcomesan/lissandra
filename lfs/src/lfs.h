#ifndef LFS_LFS_H_
#define LFS_LFS_H_

#include "memtable.h"

#include <ker/defines.h>
#include <cx/net.h>
#include <cx/pool.h>

#include <commons/config.h>
#include <commons/log.h>
#include <commons/collections/dictionary.h>

typedef enum LFS_ERR_CODE
{
    LFS_ERR_NONE = 0,
    LFS_ERR_LOGGER_FAILED,
    LFS_ERR_INIT_HALLOC,
    LFS_ERR_INIT_THREADPOOL,
    LFS_ERR_CFG_NOTFOUND,
    LFS_ERR_CFG_MISSINGKEY,
    LFS_ERR_NET_FAILED,
} LFS_ERRR_CODE;

typedef struct cfg_t
{
    t_config*               handle;             // pointer to so-commons-lib config adt.
    char                    listeningIp[16];    // ip address on which the LFS server will listen on.
    uint16_t                listeningPort;      // tcp port on which the LFS server will listen on.
    uint16_t                workers;            // number of worker threads to spawn to process requests.
    char                    rootDir[PATH_MAX];  // initial root directory of our filesystem.
    uint32_t                delay;              // artificial delay in ms for each operation performed.
    uint16_t                valueSize;          // size in bytes of a value field in a table record.
    uint32_t                dumpInterval;       // interval in ms to perform memtable dumps.
} cfg_t;

typedef struct request_t
{
    REQ_STATE               state;                          // the current state of our request.
    REQ_ORIGIN              origin;                         // origin of this request. it can be either command line interface or sockets api.
    REQ_TYPE                type;                           // the requested operation.
    uint16_t                clientHandle;                   // the handle to the client which requested this request in our server context. INVALID_HANDLE means a CLI-issued request.
    void*                   data;                           // the data (arguments and result) of the requested operation. see data_*_t structures in ker/defines.h.
} request_t;

typedef struct table_t
{
    table_meta_t            meta;                           // table metadata
    memtable_t*             memtable;                       // memtable for this table
} table_t;

typedef struct lfs_ctx_t
{
    cfg_t                   cfg;                                        // lfs configuration data.
    t_log*                  log;                                        // pointer to so-commons-lib log adt.
    bool                    isRunning;                                  // true if the server is running. false if it's shutting down.
    cx_net_ctx_sv_t*        sv;                                         // server context for serving API requests coming from MEM nodes.
    char                    buffer[MAX_PACKET_LEN - MIN_PACKET_LEN];    // temporary pre-allocated buffer for building packets.
    request_t               requests[MAX_CONCURRENT_REQUESTS];          // container for storing incoming requests during ready/running/completed states.
    cx_handle_alloc_t*      requestsHalloc;                             // handle allocator for requests container.
    cx_pool_t*              pool;                                       // main pool of worker threads to process incoming requests.
    t_dictionary*           tables;                                     // container for storing table_t entries indexed by table name.

} lfs_ctx_t;

extern lfs_ctx_t            g_ctx;

#endif // LFS_LFS_
