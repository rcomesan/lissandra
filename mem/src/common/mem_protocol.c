#include <ker/common_protocol.h>
#include <mem/mem_protocol.h>
#include <ker/ker_protocol.h>
#include <ker/defines.h>

#include <cx/binr.h>
#include <cx/binw.h>
#include <cx/mem.h>
#include <cx/str.h>

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef MEM

#include "../mem.h"
#include <ker/gossip.h>

void mem_handle_auth(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    cx_net_ctx_sv_t* sv = (cx_net_ctx_sv_t*)_common;
    cx_net_client_t* client = (cx_net_client_t*)_userData;
    uint32_t pos = 0;
    payload_t payload;
    uint32_t payloadSize = 0;

    password_t passwd;
    cx_binr_str(_buffer, _bufferSize, &pos, passwd, sizeof(passwd));

    if (0 == strncmp(g_ctx.cfg.password, passwd, MAX_PASSWD_LEN))
    {
        cx_net_validate(_common, client->cid.id);

        bool isGossip = false;
        cx_binr_bool(_buffer, _bufferSize, &pos, &isGossip);

        if (isGossip) // authentication request to perform gossiping
        {
            uint16_t memNumber = 0, portNumber = 0;
            cx_binr_uint16(_buffer, _bufferSize, &pos, &memNumber);
            cx_binr_uint16(_buffer, _bufferSize, &pos, &portNumber);

            if (memNumber == UINT16_MAX) // authentication request coming from KER
            {
                client->userData = NODE_KER;
                payloadSize = ker_pack_ack(payload, sizeof(payload), true, g_ctx.cfg.memNumber);
                cx_net_send(sv, KERP_ACK, payload, payloadSize, client->cid.id);
            }
            else // authentication request coming from another MEM node
            {
                gossip_add(client->ip, portNumber, memNumber);

                client->userData = NODE_MEM;
                payloadSize = mem_pack_ack(payload, sizeof(payload), true, g_ctx.cfg.memNumber, 0);
                cx_net_send(sv, MEMP_ACK, payload, payloadSize, client->cid.id);
            }
        }
        else // normal KER authentication request
        {
            client->userData = NODE_KER;
            payloadSize = ker_pack_ack(payload, sizeof(payload), false, g_ctx.cfg.memNumber);
            cx_net_send(sv, KERP_ACK, payload, payloadSize, client->cid.id);
        }
    }
    else
    {
        cx_net_disconnect(_common, client->cid.id, "authentication failed");
    }
}

void mem_handle_ack(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    cx_net_ctx_cl_t* cl = (cx_net_ctx_cl_t*)_common;
    uint32_t pos = 0;

    bool isGossip = false;
    cx_binr_bool(_buffer, _bufferSize, &pos, &isGossip);

    if (isGossip) // MEM <-> MEM gossip connection succeeded
    {
        // at this point we (MEM client context) are successfully connected to another remote MEM node server
        gossip_node_t* node = (gossip_node_t*)cl->userData;

        cx_binr_uint16(_buffer, _bufferSize, &pos, &node->number);
       
        cx_net_validate(cl, INVALID_CID);
        node->stage = GOSSIP_STAGE_ACKNOWLEDGED;
    }
    else // MEM <-> LFS connection (ACK coming from LFS node to MEM)
    {
        cx_binr_uint16(_buffer, _bufferSize, &pos, &g_ctx.cfg.valueSize);

        cx_net_validate(cl, INVALID_CID);
        g_ctx.lfsAvail = true;
        g_ctx.lfsHandshaking = false;
    }
}

void mem_handle_journal(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    cx_net_client_t* client = (cx_net_client_t*)_userData;

    TASK_ORIGIN origin = NULL == _userData 
        ? TASK_ORIGIN_CLI 
        : TASK_ORIGIN_API;

    task_t* task = taskman_create(origin, TASK_MT_JOURNAL, NULL, client->cid.id);
    if (NULL != task)
    {
        task->state = TASK_STATE_NEW;
    }
}

void mem_handle_req_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_CREATE);
    {
        task->data = common_unpack_req_create(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void mem_handle_req_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_DROP);
    {
        task->data = common_unpack_req_drop(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void mem_handle_req_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_DESCRIBE);
    {
        task->data = common_unpack_req_describe(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void mem_handle_req_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_SELECT);
    {
        task->data = common_unpack_req_select(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void mem_handle_req_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_INSERT);
    {
        task->data = common_unpack_req_insert(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void mem_handle_req_gossip(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    // KER or MEM node clients requesting our gossip table 

    cx_net_ctx_sv_t* svCtx = (cx_net_ctx_sv_t*)_common;
    cx_net_client_t* client = (cx_net_client_t*)_userData;

    payload_t payload;
    uint32_t  payloadSize = 0;

    uint8_t   header = (client->userData == NODE_KER)
        ? KERP_RES_GOSSIP
        : MEMP_RES_GOSSIP;

    gossip_export(&payload, &payloadSize);
    cx_net_send(svCtx, header, payload, payloadSize, client->cid.id);
}

void mem_handle_res_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    RES_BEGIN;
    {
        common_unpack_res_create(_buffer, _bufferSize, &bufferPos, NULL, &task->err);
    }
    RES_END;
}

void mem_handle_res_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    RES_BEGIN;
    {
        common_unpack_res_drop(_buffer, _bufferSize, &bufferPos, NULL, &task->err);
    }
    RES_END;
}

void mem_handle_res_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    RES_BEGIN;
    {
        data_describe_t* data = task->data;
        common_unpack_res_describe(_buffer, _bufferSize, &bufferPos, NULL, data, &task->err);
        complete = (0 == data->tablesRemaining);
    }
    RES_END;
}

void mem_handle_res_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    RES_BEGIN;
    {
        common_unpack_res_select(_buffer, _bufferSize, &bufferPos, NULL, (data_select_t*)task->data, &task->err);
    }
    RES_END;
}

void mem_handle_res_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    // this is actually a handler for responses received to inserts 
    // issued during the journaling process. we don't really have to 
    // show an output or process the result for this.
    // for now, we'll just log all those inserts that failed.

    uint32_t bufferPos = 0;
    uint16_t remoteId = 0;
    cx_err_t err;

    common_unpack_res_insert(_buffer, _bufferSize, &bufferPos, &remoteId, &err);
    CX_WARN(ERR_NONE == err.code, "memory journal: %s", err.desc);
}

void mem_handle_res_gossip(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    // gossip table arrived from MEM node server
    cx_net_ctx_cl_t* cl = (cx_net_ctx_cl_t*)_common;
    gossip_node_t* node = (gossip_node_t*)cl->userData;

    gossip_import((payload_t*)_buffer, _bufferSize);

    node->stage = GOSSIP_STAGE_DONE;   
    cx_net_disconnect(cl, INVALID_CID, "gossip process completed");
}

#endif // MEM

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t mem_pack_auth(char* _buffer, uint16_t _size, password_t _passwd, bool _isGossip, uint16_t _memNumber, uint16_t _memPortNumber)
{
    uint32_t pos = 0;

    cx_binw_str(_buffer, _size, &pos, _passwd);

    cx_binw_bool(_buffer, _size, &pos, _isGossip);

    if (_isGossip)
    {
        cx_binw_uint16(_buffer, _size, &pos, _memNumber);
        cx_binw_uint16(_buffer, _size, &pos, _memPortNumber);
    }

    return pos;
}

uint32_t mem_pack_ack(char* _buffer, uint16_t _size, bool _isGossip, uint16_t _memNumber, uint16_t _valueSize)
{
    uint32_t pos = 0;
    cx_binw_bool(_buffer, _size, &pos, _isGossip);
    if (_isGossip)
    {
        cx_binw_uint16(_buffer, _size, &pos, _memNumber);
    }
    else
    {
        cx_binw_uint16(_buffer, _size, &pos, _valueSize);
    }
    return pos;
}

uint32_t mem_pack_journal(char* _buffer, uint16_t _size)
{
    uint32_t pos = 0;
    return pos;
}

uint32_t mem_pack_req_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval)
{
    return common_pack_req_create(_buffer, _size, _remoteId, _tableName, _consistency, _numPartitions, _compactionInterval);
}

uint32_t mem_pack_req_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    return common_pack_req_drop(_buffer, _size, _remoteId, _tableName);
}

uint32_t mem_pack_req_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    return common_pack_req_describe(_buffer, _size, _remoteId, _tableName);
}

uint32_t mem_pack_req_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key)
{
    return common_pack_req_select(_buffer, _size, _remoteId, _tableName, _key);
}

uint32_t mem_pack_req_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint64_t _timestamp)
{
    return common_pack_req_insert(_buffer, _size, _remoteId, _tableName, _key, _value, _timestamp);
}

uint32_t mem_pack_res_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err)
{
    return common_pack_res_create(_buffer, _size, _remoteId, _err);
}

uint32_t mem_pack_res_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err)
{
    return common_pack_res_drop(_buffer, _size, _remoteId, _err);
}

uint32_t mem_pack_res_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err, const table_record_t* _record)
{
    return common_pack_res_select(_buffer, _size, _remoteId, _err, _record);
}

uint32_t mem_pack_res_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err)
{
    return common_pack_res_insert(_buffer, _size, _remoteId, _err);
}
