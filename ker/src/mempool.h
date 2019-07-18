#ifndef KER_MEMPOOL_H_
#define KER_MEMPOOL_H_

#include "metric.h"

#include <ker/defines.h>

#include <cx/cx.h>
#include <cx/net.h>
#include <cx/list.h>
#include <cx/cdict.h>

#include <stdbool.h>
#include <pthread.h>

typedef struct mempool_hints_t
{
    QUERY_TYPE          query;
    const char*         tableName;
    uint16_t            key;
    CONSISTENCY_TYPE    consistency;
} mempool_hints_t;

typedef struct mempool_metrics_t
{
    uint32_t            reads[CONSISTENCY_COUNT];           //
    uint32_t            writes[CONSISTENCY_COUNT];          //
    double              readLatency[CONSISTENCY_COUNT];     //
    double              writeLatency[CONSISTENCY_COUNT];    //
    double              memLoad[MAX_MEM_NODES + 1];         //
} mempool_metrics_t;

typedef struct mem_node_t
{
    uint16_t            number;                             // MEM node number (unique identifier in the pool assigned to each node).
    bool                available;
    bool                handshaking;                        // true if we're authenticating with the MEM node.
    bool                known;                              // true if this MEM node was added at some point using mempool_add so that we know its ip address and port.
    ipv4_t              ip;                                 // ip address on which the MEM node will listen on.
    uint16_t            port;                               // tcp port on which the MEM node will listen on.
    cx_list_node_t*     listNode[CONSISTENCY_COUNT];        // pointer to the node in the linked list of each criteria (only if this node is assigned to it).
    pthread_mutex_t     listNodeMtx;                        // mutex to modify listNode from multiple threads safely.
    cx_net_ctx_cl_t*    conn;                               // client context for communicating with the MEM node.
    pthread_rwlock_t    rwl;                                // read-write lock to safely access and destroy this MEM node client connection context.
    metric_t*           mtLoad;                             // metrics for read+write operations (SELECT+INSERT queries) (for this specific memory node).
} mem_node_t;

typedef struct criteria_t
{
    CONSISTENCY_TYPE    type;                               // type identifier of this criteria (consistencies defined in CONSISTENCY_TYPE enum).
    cx_list_t*          assignedNodes;                      // linked list containing all the assigned MEM nodes to this criterion.
    pthread_mutex_t     mtx;                                // mutex to protect assignedNodes linked-list when accessed from multiple threads.
    metric_t*           mtReads;                            // metrics for read operations (SELECT queries) (for this specific criterion only).
    metric_t*           mtWrites;                           // metrics for write operations (INSERT queries) (for this specific criterion only).
} criteria_t;

typedef struct mempool_ctx_t
{
    mem_node_t          nodes[MAX_MEM_NODES + 1];           // array containing all of the possible MEM nodes allowed. indexed by MEM number starting at index 1.
    criteria_t          criteria[CONSISTENCY_COUNT];        // container for each criterion supported.
    cx_cdict_t*         tablesMap;                          // dictionary for storing table_meta_t indexed by table name.
    mempool_metrics_t   metrics;                            // pool metrics sampled 
    metric_t*           mtLoad;                             // metrics for read+write operations (SELECT+INSERT queries) (mempool-wide).
} mempool_ctx_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool        mempool_init(cx_err_t* _err);

void        mempool_disconnect();

void        mempool_destroy();

void        mempool_poll_events();

void        mempool_feed_tables(table_meta_t* _tables, uint32_t _tablesCount);

void        mempool_remove_tables(table_meta_t* _tables, uint32_t _tablesCount);

void        mempool_add(uint16_t _memNumber, ipv4_t _ip, uint16_t _port);

bool        mempool_assign(uint16_t _memNumber, CONSISTENCY_TYPE _type, cx_err_t* _err);

uint16_t    mempool_get(mempool_hints_t* _hints, cx_err_t* _err);

uint16_t    mempool_get_any(cx_err_t* _err);

bool        mempool_journal(cx_err_t* _err);

int32_t     mempool_node_req(uint16_t _memNumber, uint8_t _header, const char* _payload, uint32_t _payloadSize);

void        mempool_node_wait(uint16_t _memNumber);

void        mempool_metrics_hit(uint16_t _memNumber, QUERY_TYPE _type, CONSISTENCY_TYPE _cons, double _timeElapsed);

void        mempool_metrics_update();

void        mempool_metrics_get(const mempool_metrics_t** _outMtr);

void        mempool_print();

#endif // KER_MEMPOOL_H_