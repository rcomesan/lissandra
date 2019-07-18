#include "ker.h"
#include "mempool.h"

#include <ker/ker_protocol.h>
#include <ker/taskman.h>
#include <mem/mem_protocol.h>

#include <cx/mem.h>
#include <cx/math.h>
#include <cx/str.h>

#include <inttypes.h>

static mempool_ctx_t*   m_mempoolCtx = NULL;

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool             _valid_mem_number(uint16_t _memNumber, cx_err_t* _err);

static bool             _valid_consistency(CONSISTENCY_TYPE _type, cx_err_t* _err);

static void             _on_connected_to_mem(cx_net_ctx_cl_t* _ctx);

static void             _on_disconnected_from_mem(cx_net_ctx_cl_t* _ctx);

static bool             _task_req_abort(task_t* _task, void* _userData);

static void             _request_mem_journal(cx_list_t* _list, cx_list_node_t* _node, uint32_t _index, void* _userData);

static void             _metrics_reset();

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool mempool_init(cx_err_t* _err)
{
    CX_CHECK(NULL == m_mempoolCtx, "mempool is already initialized!");

    m_mempoolCtx = CX_MEM_STRUCT_ALLOC(m_mempoolCtx);
    CX_ERR_CLEAR(_err);

    m_mempoolCtx->tablesMap = cx_cdict_init();
    if (NULL == m_mempoolCtx->tablesMap)
    {
        CX_ERR_SET(_err, ERR_INIT_CDICT, "tablesMap concurrent dictionary creation failed!");
        return false;
    }

    m_mempoolCtx->mtLoad = metric_init();
    if (NULL == m_mempoolCtx->mtLoad)
    {
        CX_ERR_SET(_err, ERR_INIT_METRIC, "mempool metric load creation failed!");
        return false;
    }

    CX_MEM_ZERO(m_mempoolCtx->nodes);
    for (uint32_t i = 1; i < MAX_MEM_NODES; i++)
    {
        mem_node_t* memNode = &m_mempoolCtx->nodes[i];
        memNode->number = i;

        if (0 != pthread_rwlock_init(&memNode->rwl, NULL))
        {
            CX_ERR_SET(_err, ERR_INIT_RWL, "pool node read-write lock creation failed!");
            return false;
        }

        if (0 != pthread_mutex_init(&memNode->listNodeMtx, NULL))
        {
            CX_ERR_SET(_err, ERR_INIT_MTX, "pool listNodes mtx creation failed!");
            return false;
        }

        memNode->mtLoad = metric_init();
        if (NULL == memNode->mtLoad)
        {
            CX_ERR_SET(_err, ERR_INIT_METRIC, "pool node metric load creation failed!");
            return false;
        }

        m_mempoolCtx->metrics.memLoad[i] = -1;
    }

    for (uint32_t i = 0; i < CONSISTENCY_COUNT; i++)
    {
        criteria_t* criteria = &m_mempoolCtx->criteria[i];

        criteria->assignedNodes = cx_list_init();
        if (NULL == criteria->assignedNodes)
        {
            CX_ERR_SET(_err,  ERR_INIT_LIST, "assigned nodes list creation failed!");
            return false;
        }

        if (0 != pthread_mutex_init(&criteria->mtx, NULL))
        {
            CX_ERR_SET(_err, ERR_INIT_MTX, "assigned nodes mutex creation failed!");
            return false;
        }

        criteria->mtReads = metric_init();
        criteria->mtWrites = metric_init();
        if (NULL == criteria->mtReads || NULL == criteria->mtWrites)
        {
            CX_ERR_SET(_err, ERR_INIT_METRIC, "criteria reads/writes metrics creation failed!");
            return false;
        }

    }

    return true;
}

void mempool_disconnect()
{
    if (NULL == m_mempoolCtx) return;

    for (uint32_t i = 1; i < MAX_MEM_NODES; i++)
    {
        mem_node_t* memNode = &m_mempoolCtx->nodes[i];
        if (NULL != memNode->conn)
        {
            cx_net_disconnect(memNode->conn, INVALID_CID, "mempool is shutting down");
            cx_net_destroy(memNode->conn);
            memNode->conn = NULL;
        }
    }
}

void mempool_destroy()
{
    if (NULL == m_mempoolCtx) return;
    
    for (uint32_t i = 1; i < MAX_MEM_NODES; i++)
    {
        mem_node_t* memNode = &m_mempoolCtx->nodes[i];

        CX_CHECK(NULL == memNode->conn, "you must call mempool_disconnect() first!");
        pthread_rwlock_destroy(&memNode->rwl);        
        pthread_mutex_destroy(&memNode->listNodeMtx);

        if (NULL != memNode->mtLoad)
        {
            metric_destroy(memNode->mtLoad);
            memNode->mtLoad = NULL;
        }
    }

    if (NULL != m_mempoolCtx->tablesMap)
    {
        cx_cdict_destroy(m_mempoolCtx->tablesMap, (cx_destroyer_cb)free);
        m_mempoolCtx->tablesMap = NULL;
    }

    if (NULL != m_mempoolCtx->mtLoad)
    {
        metric_destroy(m_mempoolCtx->mtLoad);
        m_mempoolCtx->mtLoad = NULL;
    }

    for (uint32_t i = 0; i < CONSISTENCY_COUNT; i++)
    {
        criteria_t* criteria = &m_mempoolCtx->criteria[i];

        if (NULL != criteria->assignedNodes)
        {
            cx_list_destroy(criteria->assignedNodes, free);
            criteria->assignedNodes = NULL;
        }

        pthread_mutex_destroy(&criteria->mtx);

        if (NULL != criteria->mtReads)
        {
            metric_destroy(criteria->mtReads);
            criteria->mtReads = NULL;
        }

        if (NULL != criteria->mtWrites)
        {
            metric_destroy(criteria->mtWrites);
            criteria->mtWrites = NULL;
        }
    }

    free(m_mempoolCtx);
    m_mempoolCtx = NULL;
}

void mempool_poll_events()
{
    mem_node_t* memNode = NULL;

    for (uint32_t i = 1; i < MAX_MEM_NODES; i++)
    {
        memNode = &m_mempoolCtx->nodes[i];
        
        if (NULL != memNode->conn)
        {
            if (!memNode->handshaking && (!(CX_NET_STATE_CONNECTED & memNode->conn->c.state) || !memNode->available))
            {
                pthread_rwlock_wrlock(&memNode->rwl);
                cx_net_destroy((void*)memNode->conn);
                memNode->conn = NULL;
                pthread_rwlock_unlock(&memNode->rwl);
            }
            else
            {
                cx_net_poll_events(memNode->conn, 0);
            }
        }
    }
}

void mempool_feed_tables(table_meta_t* _tables, uint32_t _tablesCount)
{
    table_meta_t* table = NULL;
    table_meta_t* curTable = NULL;

    pthread_mutex_lock(&m_mempoolCtx->tablesMap->mtx);

    if (_tablesCount > 1)
    {
        // global describe, we can safely remove them all and start over with fresh metadata
        cx_cdict_clear(m_mempoolCtx->tablesMap, (cx_destroyer_cb)free);
    }

    for (uint32_t i = 0; i < _tablesCount; i++)
    {
        if (!cx_cdict_contains(m_mempoolCtx->tablesMap, _tables[i].name))
        {
            table = CX_MEM_STRUCT_ALLOC(table);
            memcpy(table, &_tables[i], sizeof(*table));
            cx_cdict_set(m_mempoolCtx->tablesMap, table->name, table);
        }
    }
    pthread_mutex_unlock(&m_mempoolCtx->tablesMap->mtx);
}

void mempool_add(uint16_t _memNumber, ipv4_t _ip, uint16_t _port)
{
    // warning: this is not thread safe. it must be called from MT only.

    if (!_valid_mem_number(_memNumber, NULL))
    {
        CX_WARN(CX_ALW, "ignoring mempool add for MEM node #%d (invalid memNumber)!", _memNumber);
        return;
    }

    mem_node_t* memNode = &m_mempoolCtx->nodes[_memNumber];

    if (NULL == memNode->conn)
    {
        memNode->known = true;
        memNode->handshaking = true;
        cx_str_copy(memNode->ip, sizeof(memNode->ip), _ip);
        memNode->port = _port;

        // establish a new client connection with this MEM node.
        cx_net_args_t args;
        CX_MEM_ZERO(args);

        cx_str_format(args.name, sizeof(args.name), "mem-%d", _memNumber);
        cx_str_copy(args.ip, sizeof(args.ip), memNode->ip);
        args.port = memNode->port;
        args.userData = (void*)memNode;
        args.multiThreadedSend = true;
        args.connectBlocking = false;
        args.onConnected = (cx_net_on_connected_cb)_on_connected_to_mem;
        args.onDisconnected = (cx_net_on_connected_cb)_on_disconnected_from_mem;

        // message headers to handlers mappings
        args.msgHandlers[KERP_ACK] = (cx_net_handler_cb)ker_handle_ack;
        args.msgHandlers[KERP_RES_CREATE] = (cx_net_handler_cb)ker_handle_res_create;
        args.msgHandlers[KERP_RES_DROP] = (cx_net_handler_cb)ker_handle_res_drop;
        args.msgHandlers[KERP_RES_DESCRIBE] = (cx_net_handler_cb)ker_handle_res_describe;
        args.msgHandlers[KERP_RES_SELECT] = (cx_net_handler_cb)ker_handle_res_select;
        args.msgHandlers[KERP_RES_INSERT] = (cx_net_handler_cb)ker_handle_res_insert;

        // start client context
        memNode->conn = cx_net_connect(&args);
        CX_WARN(NULL != memNode->conn, "memNode client context creation failed!");
    }
}

bool mempool_assign(uint16_t _memNumber, CONSISTENCY_TYPE _type, cx_err_t* _err)
{
    // this should be thread safe since it could be called from multiple worker threads 
    // at the same time (multiple ADD commands on different scripts could call this method)

    if (!_valid_mem_number(_memNumber, _err)) return false;
    if (!_valid_consistency(_type, _err)) return false;

    bool success = false;
    mem_node_t* memNode = &m_mempoolCtx->nodes[_memNumber];
    criteria_t* criteria = &m_mempoolCtx->criteria[_type];

    pthread_mutex_lock(&memNode->listNodeMtx);
    if (memNode->known)
    {
        if (memNode->available)
        {
            if (NULL == memNode->listNode[_type])
            {
                pthread_mutex_lock(&criteria->mtx);
                if (CONSISTENCY_STRONG == _type)
                {
                    success = (0 == cx_list_size(criteria->assignedNodes));
                    if (!success)
                    {
                        mem_node_t* curMemNode = (mem_node_t*)cx_list_peek_front(criteria->assignedNodes)->data;
                        CX_ERR_SET(_err, ERR_GENERIC, "%s consistency already has MEM node #%d assigned.",
                            CONSISTENCY_NAME[_type], curMemNode->number);
                    }
                }
                else if (CONSISTENCY_STRONG_HASHED == _type)
                {
                    success = true;
                    cx_list_foreach(criteria->assignedNodes, (cx_list_func_cb)_request_mem_journal, NULL);
                }
                else if (CONSISTENCY_EVENTUAL == _type)
                {
                    success = true;
                }
                else if (CONSISTENCY_NONE == _type)
                {
                    success = true;
                }

                if (success)
                {
                    memNode->listNode[_type] = cx_list_node_alloc(memNode);
                    cx_list_push_front(criteria->assignedNodes, memNode->listNode[_type]);

                    CX_INFO("MEM node #%d assigned to %s consistency.",
                        memNode->number, CONSISTENCY_NAME[_type]);
                }
                pthread_mutex_unlock(&criteria->mtx);
            }
            else
            {
                CX_ERR_SET(_err, ERR_GENERIC, "MEM node #%d is already assigned to %s consistency.",
                    _memNumber, CONSISTENCY_NAME[_type]);
            }
        }
        else
        {
            CX_ERR_SET(_err, ERR_GENERIC, "MEM node #%d is down.", _memNumber);
        }
    }
    else
    {
        CX_ERR_SET(_err, ERR_GENERIC, "MEM node #%d is unknown.", _memNumber);
    }
    pthread_mutex_unlock(&memNode->listNodeMtx);

    return success;
}

uint16_t mempool_get(mempool_hints_t* _hints, cx_err_t* _err)
{
    uint16_t            memNumber = INVALID_MEM_NUMBER;
    mem_node_t*         memNode = NULL;
    cx_list_node_t*     listNode = NULL;
    bool                consFound = true;
    CONSISTENCY_TYPE    cons = CONSISTENCY_NONE;

    if (QUERY_CREATE == _hints->query)
    {
        cons = _hints->consistency;
    }
    else if (QUERY_DESCRIBE == _hints->query)
    {
        return mempool_get_any(_err);
    }
    else if (NULL != _hints->tableName)
    {
        table_meta_t* meta = NULL;

        pthread_mutex_lock(&m_mempoolCtx->tablesMap->mtx);
        if (cx_cdict_get(m_mempoolCtx->tablesMap, _hints->tableName, (void**)&meta))
        {
            cons = (CONSISTENCY_TYPE)meta->consistency;
        }
        else
        {
            CX_ERR_SET(_err, ERR_GENERIC, "Table '%s' does not exist.", _hints->tableName);
            consFound = false;
        }
        pthread_mutex_unlock(&m_mempoolCtx->tablesMap->mtx);
    }
    else
    {
        CX_ERR_SET(_err, ERR_GENERIC, "Table name hint is required for query #%d.", _hints->query);
        consFound = false;
    }

    if (consFound && _valid_consistency(cons, _err))
{
        criteria_t* criteria = &m_mempoolCtx->criteria[cons];

        pthread_mutex_lock(&criteria->mtx);

        if (cx_list_size(criteria->assignedNodes) > 0)
        {
            if (CONSISTENCY_STRONG == cons)
            {
                listNode = cx_list_peek_front(criteria->assignedNodes);
            }
            else if (CONSISTENCY_STRONG_HASHED == cons)
            {
                uint32_t index = _hints->key % cx_list_size(criteria->assignedNodes);
                listNode = cx_list_get(criteria->assignedNodes, index);
            }
            else
            {
                listNode = cx_list_pop_front(criteria->assignedNodes);
                cx_list_push_back(criteria->assignedNodes, listNode);
            }
        }

        if (NULL != listNode)
        {
            memNode = (mem_node_t*)listNode->data;
            memNumber = memNode->number;
        }
        pthread_mutex_unlock(&criteria->mtx);

        if (NULL == memNode)
            CX_ERR_SET(_err, ERR_GENERIC, "There're no MEM nodes satisfying criteria.")
    }

    _hints->consistency = cons;
    return memNumber;
}

uint16_t mempool_get_any(cx_err_t* _err)
{
    uint16_t        memNumber = INVALID_MEM_NUMBER;
    mem_node_t*     memNode = NULL;
    cx_list_node_t* listNode = NULL;
    
    criteria_t* criteria = &m_mempoolCtx->criteria[CONSISTENCY_NONE];

    pthread_mutex_lock(&criteria->mtx);

    if (cx_list_size(criteria->assignedNodes) > 0)
    {
        listNode = cx_list_pop_front(criteria->assignedNodes);
        cx_list_push_back(criteria->assignedNodes, listNode);
    }

    if (NULL != listNode)
    {
        memNode = (mem_node_t*)listNode->data;
        memNumber = memNode->number;
    }

    pthread_mutex_unlock(&criteria->mtx);

    if (NULL == memNode)
        CX_ERR_SET(_err, ERR_GENERIC, "There're no MEM nodes available.")

    return memNumber;
}

bool mempool_journal(cx_err_t* _err)
{
    criteria_t* criteria = &m_mempoolCtx->criteria[CONSISTENCY_NONE];

    pthread_mutex_lock(&criteria->mtx);

    cx_list_foreach(criteria->assignedNodes,
        (cx_list_func_cb)_request_mem_journal, NULL);

    pthread_mutex_unlock(&criteria->mtx);

    return true;
}

int32_t mempool_node_req(uint16_t _memNumber, uint8_t _header, const char* _payload, uint32_t _payloadSize)
{
    int32_t result = CX_NET_SEND_DISCONNECTED;

    if (_valid_mem_number(_memNumber, NULL))
    {
        mem_node_t* memNode = &m_mempoolCtx->nodes[_memNumber];

        pthread_rwlock_rdlock(&memNode->rwl);
        if (NULL != memNode->conn)
        {
            result = cx_net_send(memNode->conn, _header, _payload, _payloadSize, INVALID_CID);
        }
        pthread_rwlock_unlock(&memNode->rwl);
    }

    return result;
}

void mempool_node_wait(uint16_t _memNumber)
{
    if (_valid_mem_number(_memNumber, NULL))
    {
        mem_node_t* node = &m_mempoolCtx->nodes[_memNumber];

        pthread_rwlock_rdlock(&node->rwl);
        if (NULL != node->conn)
        {
            cx_net_wait_outboundbuff(node->conn, INVALID_CID, -1);
        }
        pthread_rwlock_unlock(&node->rwl);
    }
}

void mempool_metrics_hit(uint16_t _memNumber, QUERY_TYPE _type, CONSISTENCY_TYPE _cons, double _timeElapsed)
{
    if (!_valid_mem_number(_memNumber, NULL) || !_valid_consistency(_cons, NULL))
    {
        CX_WARN(CX_ALW, "invalid mempool metric hit!");
        return;
    }

    if (QUERY_SELECT == _type)
    {
        metric_hit(m_mempoolCtx->criteria[_cons].mtReads, _timeElapsed);
    }
    else if (QUERY_INSERT == _type)
    {
        metric_hit(m_mempoolCtx->criteria[_cons].mtWrites, _timeElapsed);
    }

    if (QUERY_SELECT == _type || QUERY_INSERT == _type)
    {
        metric_hit(m_mempoolCtx->nodes[_memNumber].mtLoad, _timeElapsed);
        metric_hit(m_mempoolCtx->mtLoad, _timeElapsed);
    }
}

void mempool_metrics_update()
{
    criteria_t* crit = NULL;
    mem_node_t* node = NULL;
    uint32_t    hits = 0;
    double      timeElapsed = 0;
    double      load = 0;

    for (uint32_t i = 0; i < CONSISTENCY_COUNT; i++)
    {
        crit = &m_mempoolCtx->criteria[i];

        metric_get(crit->mtReads, &hits, &timeElapsed);
        m_mempoolCtx->metrics.reads[i] = hits;
        m_mempoolCtx->metrics.readLatency[i] = hits > 0 ? timeElapsed / hits : 0;

        metric_get(crit->mtWrites, &hits, &timeElapsed);
        m_mempoolCtx->metrics.writes[i] = hits;
        m_mempoolCtx->metrics.writeLatency[i] = hits > 0 ? timeElapsed / hits : 0;
    }

    uint32_t totalReadsWrites = 0;
    metric_get(m_mempoolCtx->mtLoad, &totalReadsWrites, NULL);

    for (uint32_t i = 1; i < MAX_MEM_NODES; i++)
    {
        node = &m_mempoolCtx->nodes[i];
        load = -1;

        if (node->known)
        {
            metric_get(node->mtLoad, &hits, &timeElapsed);

            if (node->available || hits > 0)
                load = totalReadsWrites > 0 ? ((double)hits) / totalReadsWrites : 0;
        }

        m_mempoolCtx->metrics.memLoad[i] = load;
    }

    // reset everything to start from scratch for the next sampling request
    _metrics_reset();
}

void mempool_metrics_get(const mempool_metrics_t** _outMtr)
{
    (*_outMtr) = &m_mempoolCtx->metrics;
}

void mempool_print()
{
    mem_node_t* node = NULL;
    host_t host;

    printf("+-----+-----------------------+--------+-----+-----+-----+-----+\n");
    printf("| #   | Address:Port          | Status | %-3s | %-3s | %-3s | %-3s |\n", 
        CONSISTENCY_NAME_SHORT[CONSISTENCY_NONE],
        CONSISTENCY_NAME_SHORT[CONSISTENCY_STRONG],
        CONSISTENCY_NAME_SHORT[CONSISTENCY_STRONG_HASHED],
        CONSISTENCY_NAME_SHORT[CONSISTENCY_EVENTUAL]);
    printf("+-----+-----------------------+--------+-----+-----+-----+-----+\n");

    for (uint32_t i = 1; i < CX_ARR_SIZE(m_mempoolCtx->nodes); i++)
    {
        node = &m_mempoolCtx->nodes[i];

        if (node->known)
        {
            pthread_mutex_lock(&node->listNodeMtx);

            cx_str_format(host, sizeof(host), "%s:%" PRIu16, node->ip, node->port);
        
            printf("| %-3d | %-21s | %-6s | %-3s | %-3s | %-3s | %-3s |\n",
                i,
                host,
                node->available ? "Up" : "Down",
                NULL != node->listNode[CONSISTENCY_NONE] ? "Yes" : "",
                NULL != node->listNode[CONSISTENCY_STRONG] ? "Yes" : "",
                NULL != node->listNode[CONSISTENCY_STRONG_HASHED] ? "Yes" : "",
                NULL != node->listNode[CONSISTENCY_EVENTUAL] ? "Yes" : "");
            
            pthread_mutex_unlock(&node->listNodeMtx);
        }
    }
    printf("+-----+-----------------------+--------+-----+-----+-----+-----+\n");
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static bool _valid_mem_number(uint16_t _memNumber, cx_err_t* _err)
{
    if (!cx_math_in_range(_memNumber, 1, MAX_MEM_NODES))
    {
        CX_ERR_SET(_err, ERR_GENERIC, "MEM node #%d is not in [%d~%d] supported range.", _memNumber, 1, MAX_MEM_NODES);
        return false;
    }
    return true;
}

static bool _valid_consistency(CONSISTENCY_TYPE _type, cx_err_t* _err)
{
    if (!cx_math_in_range(_type, 0, CONSISTENCY_COUNT - 1))
    {
        CX_ERR_SET(_err, ERR_GENERIC, "Consistency %d does not exist.", _type);
        return false;
    }
    return true;
}

static void _on_connected_to_mem(cx_net_ctx_cl_t* _ctx)
{
    mem_node_t* node = (mem_node_t*)_ctx->userData;

    payload_t payload;
    uint32_t payloadSize = mem_pack_auth(payload, sizeof(payload), 
        g_ctx.cfg.memPassword, false, UINT16_MAX, UINT16_MAX);

    cx_net_send(_ctx, MEMP_AUTH, payload, payloadSize, INVALID_CID);
}

static void _on_disconnected_from_mem(cx_net_ctx_cl_t* _ctx)
{
    mem_node_t* memNode = (mem_node_t*)_ctx->userData;
 
    if (memNode->handshaking)
    {
        // handshake process failed.
        memNode->handshaking = false;
    }
    else if (memNode->available)
    {       
        // the connection with this MEM node is now gone.
        memNode->available = false;

        // unassign it from the criteria so that no worker threads pick it up
        criteria_t* criteria = NULL;

        pthread_mutex_lock(&memNode->listNodeMtx);
        for (uint8_t i = 0; i < CONSISTENCY_COUNT; i++)
        {
            if (NULL != memNode->listNode[i])
            {
                // it's assigned to criteria #i, let's remove it
                criteria = &m_mempoolCtx->criteria[i];

                pthread_mutex_lock(&criteria->mtx);
                cx_list_remove(criteria->assignedNodes, memNode->listNode[i]);
                pthread_mutex_unlock(&criteria->mtx);

                // free free the list node 
                free(memNode->listNode[i]);
                memNode->listNode[i] = NULL;
            }
        }
        pthread_mutex_unlock(&memNode->listNodeMtx);

        // let's abort and wake up all the tasks with pending requests on it.
        taskman_foreach((taskman_func_cb)_task_req_abort, (void*)((uint32_t)memNode->number));

        CX_INFO("MEM node #%d (%s:%d) just disconnected from the pool.", memNode->number, memNode->ip, memNode->port);
    }
}

static bool _task_req_abort(task_t* _task, void* _userData)
{
    uint32_t memNumber = (uint32_t)_userData; //TODO test me please

    pthread_mutex_lock(&_task->responseMtx);
    if (TASK_STATE_RUNNING_AWAITING == _task->state 
        && (0 == memNumber || _task->responseMemNumber == memNumber))
    {
        _task->state = TASK_STATE_RUNNING;
        CX_ERR_SET(&_task->err, ERR_NET_MEM_UNAVAILABLE, "MEM node #%d is unavailable.", _task->responseMemNumber);
        pthread_cond_signal(&_task->responseCond);
    }
    pthread_mutex_unlock(&_task->responseMtx);

    return true;
}

static void _request_mem_journal(cx_list_t* _list, cx_list_node_t* _node, uint32_t _index, void* _userData)
{
    mem_node_t* memNode = (mem_node_t*)_node->data;

    payload_t payload;
    uint32_t payloadSize = mem_pack_journal(payload, sizeof(payload));

    int32_t result = cx_net_send(memNode->conn, MEMP_JOURNAL, payload, payloadSize, INVALID_CID);
    CX_UNUSED(result);

    CX_WARN(result != CX_NET_SEND_BUFFER_FULL, 
        "MEM node #%d journal request failed (outbound buffer is full).", memNode->number)
}

static void _metrics_reset()
{
    criteria_t* crit = NULL;
    mem_node_t* node = NULL;

    for (uint32_t i = 0; i < CONSISTENCY_COUNT; i++)
    {
        crit = &m_mempoolCtx->criteria[i];

        metric_reset(crit->mtReads);
        metric_reset(crit->mtWrites);
    }

    for (uint32_t i = 1; i < MAX_MEM_NODES; i++)
    {
        node = &m_mempoolCtx->nodes[i];
        metric_reset(node->mtLoad);
    }

    metric_reset(m_mempoolCtx->mtLoad);
}