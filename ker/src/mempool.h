#ifndef KER_MEMPOOL_H_
#define KER_MEMPOOL_H_

#include <ker/defines.h>

#include <cx/cx.h>
#include <cx/net.h>
#include <cx/list.h>
#include <cx/cdict.h>

#include <stdbool.h>
#include <pthread.h>

typedef struct mempool_hints_t
{
    E_QUERY             query;
    const char*         tableName;
    uint16_t            key;
    E_CONSISTENCY       consistency;
} mempool_hints_t;

typedef struct mempool_metrics_t
{
    double              readLatency;                        //
    double              writeLatency;                       //
    double              readsCount;                         //
    double              writesCount;                        //
    double              memLoad[MAX_MEM_NODES + 1];         //
} mempool_metrics_t;

typedef struct metrics_t
{
    double              timeElapsed;                        // total amount of time taken in seconds to perform the current amount of operations.
    uint32_t            operations;                         // current amount of operations performed.
} metrics_t;

typedef struct mem_node_t
{
    uint16_t            number;                             // MEM node number (unique identifier in the pool assigned to each node).
    bool                handshaking;                        // true if we're authenticating with the MEM node.
    ipv4_t              ip;                                 // ip address on which the MEM node will listen on.
    uint16_t            port;                               // tcp port on which the MEM node will listen on.
    cx_list_node_t*     listNode[CONSISTENCY_COUNT];        // pointer to the node in the linked list of each criteria (only if this node is assigned to it).
    pthread_mutex_t     listNodeMtx;                        // mutex to modify listNode from multiple threads safely.
    cx_net_ctx_cl_t*    conn;                               // client context for communicating with the MEM node.
    pthread_rwlock_t    rwl;                                // read-write lock to safely access and destroy this MEM node client connection context.
} mem_node_t;

typedef struct criteria_t
{
    E_CONSISTENCY       type;                               // type identifier of this criteria (consistencies defined in CONSISTENCY_TYPE enum).
    cx_list_t*          assignedNodes;                      // linked list containing all the assigned MEM nodes to this criteria.
    pthread_mutex_t     mtx;                                // mutex to protect assignedNodes linked-list when accessed from multiple threads.
    metrics_t           metrics[QUERY_COUNT];               // metrics for each of supported type of query (defined in QUERY_TYPE enum).
} criteria_t;

typedef struct mempool_ctx_t
{
    mem_node_t          nodes[MAX_MEM_NODES + 1];           //
    uint16_t            nodesCount;                         //
    criteria_t          criteria[CONSISTENCY_COUNT];        //
    cx_cdict_t*         tablesMap;                          //
} mempool_ctx_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool        mempool_init(cx_err_t* _err);

void        mempool_disconnect();

void        mempool_destroy();

void        mempool_update();

void        mempool_feed_tables(data_describe_t* _data);

void        mempool_add(uint16_t _memNumber, ipv4_t _ip, uint16_t _port);

bool        mempool_assign(uint16_t _memNumber, E_CONSISTENCY _type, cx_err_t* _err);

uint16_t    mempool_get(mempool_hints_t* _hints, cx_err_t* _err);

uint16_t    mempool_get_any(cx_err_t* _err);

int32_t     mempool_node_req(uint16_t _memNumber, uint8_t _header, const char* _payload, uint32_t _payloadSize);

void        mempool_node_wait(uint16_t _memNumber);

void        mempool_metrics_report(uint16_t _memNumber, E_QUERY _type, double _timeElapsed);

void        mempool_metrics_get(mempool_metrics_t* _outData);

#endif // KER_MEMPOOL_H_