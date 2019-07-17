#include <ker/gossip.h>
#include <ker/ker_protocol.h>
#include <mem/mem_protocol.h>

#ifdef KER
#include "../mempool.h"
#endif

#include <cx/mem.h>
#include <cx/net.h>
#include <cx/str.h>
#include <cx/timer.h>
#include <cx/binw.h>
#include <cx/binr.h>

#include <inttypes.h>

static gossip_ctx_t*    m_gossipCtx = NULL;

#define GOSSIP_USAGE_RESTRICTION \
    CX_CHECK(CX_ALW, "this module should only be used on KER/MEM nodes!");

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void             _node_destroyer(void* _data);

static void             _on_connected_to_mem(cx_net_ctx_cl_t* _ctx);

static void             _on_disconnected_from_mem(cx_net_ctx_cl_t* _ctx);

static void             _node_key(const ipv4_t _ip, uint16_t _port, node_key_t* _outKey);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool gossip_init(const seed_t* _seeds, uint16_t _seedsCount, cx_err_t* _err)
{
    CX_CHECK(NULL == m_gossipCtx, "gossip module is already initialized!");

    m_gossipCtx = CX_MEM_STRUCT_ALLOC(m_gossipCtx);
    CX_ERR_CLEAR(_err);

    m_gossipCtx->nodes = cx_cdict_init();
    if (NULL == m_gossipCtx->nodes)
    {
        CX_ERR_SET(_err, ERR_INIT_CDICT, "gossip nodes dictionary creation failed!");
        return false;
    }

    for (uint16_t i = 0; i < _seedsCount; i++)
    {
        gossip_add(_seeds[i].ip, _seeds[i].port, MEM_NUMBER_UNKNOWN);
    }

    return true;
}

void gossip_destroy()
{
    if (NULL == m_gossipCtx) return;

    if (NULL != m_gossipCtx->nodes)
    {
        cx_cdict_destroy(m_gossipCtx->nodes, (cx_destroyer_cb)_node_destroyer);
    }

    free(m_gossipCtx);
    m_gossipCtx = NULL;
}

void gossip_add(const ipv4_t _ip, uint16_t _port, uint16_t _memNumber)
{
    node_key_t key;
    _node_key(_ip, _port, &key);

#if defined(MEM)
    // make sure we aren't adding ourselves to our gossip table!
    if (0 == strcasecmp(g_ctx.cfg.listeningIp, _ip) && g_ctx.cfg.listeningPort == _port) 
        return;
#endif

    if (!cx_cdict_contains(m_gossipCtx->nodes, key))
    {
        gossip_node_t* node = CX_MEM_STRUCT_ALLOC(node);
        node->stage = GOSSIP_STAGE_NONE;
        node->number = _memNumber;
        cx_str_copy(node->ip, sizeof(node->ip), _ip);
        node->port = _port;
        node->conn = NULL;
        node->available = true; // new gossip nodes are initially assumed to be available

        cx_cdict_set(m_gossipCtx->nodes, key, node);
    }
}

void gossip_run()
{
    if (NULL == m_gossipCtx) return;

    gossip_node_t* node = NULL;
    char* key;

    cx_cdict_iter_begin(m_gossipCtx->nodes);
    while (cx_cdict_iter_next(m_gossipCtx->nodes, &key, (void**)&node))
    {

#ifdef KER
        if (MEM_NUMBER_UNKNOWN != node->number && node->available)
        {
            mempool_add(node->number, node->ip, node->port);
        }
#endif

        if (GOSSIP_STAGE_NONE == node->stage && NULL == node->conn)
        {        
            node->stage = GOSSIP_STAGE_HANDSHAKING;

            cx_net_args_t args;
            CX_MEM_ZERO(args);

            cx_str_format(args.name, sizeof(args.name), "gossip-%s", key);
            cx_str_copy(args.ip, sizeof(args.ip), node->ip);
            args.port = node->port;
            args.userData = (void*)node;
            args.multiThreadedSend = false;
            args.connectBlocking = false;
            args.onConnected = (cx_net_on_connected_cb)_on_connected_to_mem;
            args.onDisconnected = (cx_net_on_disconnected_cb)_on_disconnected_from_mem;

#if defined(KER)
            args.msgHandlers[KERP_ACK] = (cx_net_handler_cb)ker_handle_ack;
            args.msgHandlers[KERP_RES_GOSSIP] = (cx_net_handler_cb)ker_handle_res_gossip;
#elif defined(MEM)
            args.msgHandlers[MEMP_ACK] = (cx_net_handler_cb)mem_handle_ack;
            args.msgHandlers[MEMP_RES_GOSSIP] = (cx_net_handler_cb)mem_handle_res_gossip;
#else
            GOSSIP_USAGE_RESTRICTION;
#endif

            // start client context
            node->conn = cx_net_connect(&args);
            if (NULL == node->conn)
            {
                node->stage = GOSSIP_STAGE_NONE;
                CX_WARN(CX_ALW, "gossip node client context creation failed!");
            }
        }
    }
    cx_cdict_iter_end(m_gossipCtx->nodes);
}

void gossip_poll_events()
{
    if (NULL == m_gossipCtx) return;

    gossip_node_t* node = NULL;

    cx_cdict_iter_begin(m_gossipCtx->nodes);
    while (cx_cdict_iter_next(m_gossipCtx->nodes, NULL, (void**)&node))
    {
        if (NULL != node->conn)
        {
            if (GOSSIP_STAGE_HANDSHAKING != node->stage
                && (!(CX_NET_STATE_CONNECTED & node->conn->c.state) || !node->available))
            {
                cx_net_destroy(node->conn);
                node->conn = NULL;
            }
            else
            {
                cx_net_poll_events(node->conn, 0);
            }
        }

        if (GOSSIP_STAGE_ACKNOWLEDGED == node->stage)
        {
            node->stage = GOSSIP_STAGE_REQUESTING;
            cx_net_send(node->conn, MEMP_REQ_GOSSIP, NULL, 0, INVALID_HANDLE);
        }
    }
    cx_cdict_iter_end(m_gossipCtx->nodes);
}

void gossip_export(payload_t* _payload, uint32_t* _payloadSize)
{
    gossip_node_t* node = NULL;
    uint16_t count = 0;
    uint32_t pos = 0;

    cx_cdict_iter_begin(m_gossipCtx->nodes);
    while (cx_cdict_iter_next(m_gossipCtx->nodes, NULL, (void**)&node))
    {
        if (node->available)
            count++;
    }

    cx_binw_uint16(*_payload, sizeof(*_payload), &pos, count);

    if (count > 0)
    {
        cx_cdict_iter_first(m_gossipCtx->nodes);
        while (cx_cdict_iter_next(m_gossipCtx->nodes, NULL, (void**)&node))
        {
            if (node->available)
            {
                cx_binw_uint16(*_payload, sizeof(*_payload), &pos, node->number);
                cx_binw_str(*_payload, sizeof(*_payload), &pos, node->ip);
                cx_binw_uint16(*_payload, sizeof(*_payload), &pos, node->port);
            }
        }
    }
    cx_cdict_iter_end(m_gossipCtx->nodes);

    (*_payloadSize) = pos;
}

void gossip_import(payload_t* _payload, uint32_t _payloadSize)
{
    uint16_t    count = 0;
    uint32_t    pos = 0;
    uint16_t    number;
    ipv4_t      ip;
    uint16_t    port;

    cx_binr_uint16(*_payload, sizeof(*_payload), &pos, &count);

    for (uint16_t i = 0; i < count; i++)
    {
        cx_binr_uint16(*_payload, sizeof(*_payload), &pos, &number);
        cx_binr_str(*_payload, sizeof(*_payload), &pos, ip, sizeof(ip));
        cx_binr_uint16(*_payload, sizeof(*_payload), &pos, &port);

        gossip_add(ip, port, number);
    }

    CX_CHECK(_payloadSize == pos, "some bytes were not consumed from the stream!");
}

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void _node_destroyer(void* _data)
{
    gossip_node_t* node = (gossip_node_t*)_data;

    if (NULL != node->conn)
    {
        cx_net_destroy(node->conn);
        node->conn = NULL;
    }

    free(_data);
}

static void _on_connected_to_mem(cx_net_ctx_cl_t* _ctx)
{
    gossip_node_t* node = (gossip_node_t*)_ctx->userData;

    payload_t payload;
    uint32_t  payloadSize = 0;

#if defined (KER)
    payloadSize = mem_pack_auth(payload, sizeof(payload),
        g_ctx.cfg.memPassword, true, UINT16_MAX, UINT16_MAX);
#elif defined (MEM)
    payloadSize = mem_pack_auth(payload, sizeof(payload),
        g_ctx.cfg.password, true, g_ctx.cfg.memNumber, g_ctx.cfg.listeningPort);
#else
    GOSSIP_USAGE_RESTRICTION;
#endif
    
    cx_net_send(_ctx, MEMP_AUTH, payload, payloadSize, INVALID_HANDLE);
}

static void _on_disconnected_from_mem(cx_net_ctx_cl_t* _ctx)
{
    gossip_node_t* node = (gossip_node_t*)_ctx->userData;

    if (GOSSIP_STAGE_DONE == node->stage)
    {
        // gossip exchange succeeded, we're now disconnected from the node
        node->available = true;
        node->lastGossipTime = cx_time_counter();
    }
    else
    {
        // node went donw/connection failed/handshake failed/disconnected
        // we will flag it as not available so that the mempool doesn't consume it, 
        // but we will keep trying to reconnect to it in following gossip processes
        node->available = false;
    }

    node->stage = GOSSIP_STAGE_NONE;
}

static void _node_key(const ipv4_t _ip, uint16_t _port, node_key_t* _outKey)
{
    cx_str_format(*_outKey, sizeof(*_outKey), "%s:%" PRIu16, _ip, _port);
}