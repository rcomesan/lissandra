#include <ker/common_protocol.h>
#include <ker/ker_protocol.h>
#include <ker/defines.h>

#include <cx/binr.h>
#include <cx/binw.h>
#include <cx/mem.h>
#include <cx/str.h>

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef KER

#include "../ker.h"
#include "../mempool.h"
#include "ker/gossip.h"
#include "mem/mem_protocol.h"

void ker_handle_ack(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    cx_net_ctx_cl_t* cl = (cx_net_ctx_cl_t*)_common;
    uint32_t pos = 0;
    bool isGossip = false;

    cx_binr_bool(_buffer, _bufferSize, &pos, &isGossip);

    if (isGossip) // KER <-> MEM gossip connection succeeded
    {
        gossip_node_t* node = (gossip_node_t*)cl->userData;

        cx_binr_uint16(_buffer, _bufferSize, &pos, &node->number);

        cx_net_validate(cl, INVALID_CID);
        node->stage = GOSSIP_STAGE_ACKNOWLEDGED;
    }
    else // normal KER <-> MEM connection succeeded
    {
        mem_node_t* node = (mem_node_t*)cl->userData;

        cx_net_validate(cl, INVALID_CID);
        node->handshaking = false;
        node->available = true;

        // by default, assign this memory to CONSISTENCY_NONE criterion.
        mempool_assign(node->number, CONSISTENCY_NONE, NULL);
    }
}

void ker_handle_req_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_CREATE);
    {
        task->data = common_unpack_req_create(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void ker_handle_req_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_DROP);
    {
        task->data = common_unpack_req_drop(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void ker_handle_req_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_DESCRIBE);
    {
        task->data = common_unpack_req_describe(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void ker_handle_req_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_SELECT);
    {
        task->data = common_unpack_req_select(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void ker_handle_req_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_INSERT);
    {
        task->data = common_unpack_req_insert(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void ker_handle_req_journal(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_JOURNAL);
    {
        task->data = NULL;
    }
    REQ_END;
}

void ker_handle_req_addmem(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_ADDMEM);
    {
        data_addmem_t* data = CX_MEM_STRUCT_ALLOC(data);
        cx_binr_uint16(_buffer, _bufferSize, &bufferPos, &data->memNumber);
        cx_binr_uint8(_buffer, _bufferSize, &bufferPos, &data->consistency);

        task->data = data;
    }
    REQ_END;
}

void ker_handle_req_run(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_RUN);
    {
        data_run_t* data = CX_MEM_STRUCT_ALLOC(data);

        cx_binr_str(_buffer, _bufferSize, &bufferPos, data->scriptFilePath, sizeof(data->scriptFilePath));
        cx_file_get_name(&data->scriptFilePath, false, &data->scriptFileName);
        data->script = NULL;

        cx_binr_str(_buffer, _bufferSize, &bufferPos, data->outputFilePath, sizeof(data->outputFilePath));
        data->output = NULL;

        data->lineNumber = 0;

        task->data = data;
    }
    REQ_END;
}

void ker_handle_res_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    RES_BEGIN;
    {
        common_unpack_res_create(_buffer, _bufferSize, &bufferPos, NULL, &task->err);
    }
    RES_END;
}

void ker_handle_res_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    RES_BEGIN;
    {
        common_unpack_res_drop(_buffer, _bufferSize, &bufferPos, NULL, &task->err);
    }
    RES_END;
}

void ker_handle_res_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    RES_BEGIN;
    {
        data_describe_t* data = task->data;
        common_unpack_res_describe(_buffer, _bufferSize, &bufferPos, NULL, data, &task->err);
        complete = (0 == data->tablesRemaining);
    }
    RES_END;
}

void ker_handle_res_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    RES_BEGIN;
    {
        common_unpack_res_select(_buffer, _bufferSize, &bufferPos, NULL, (data_select_t*)task->data, &task->err);
    }
    RES_END;
}

void ker_handle_res_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    RES_BEGIN;
    {
        common_unpack_res_insert(_buffer, _bufferSize, &bufferPos, NULL, &task->err);
    }
    RES_END;
}

void ker_handle_res_gossip(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    // gossip table arrived from MEM node server
    cx_net_ctx_cl_t* cl = (cx_net_ctx_cl_t*)_common;
    gossip_node_t* node = (gossip_node_t*)cl->userData;

    gossip_import((payload_t*)_buffer, _bufferSize);

    node->stage = GOSSIP_STAGE_DONE;
    cx_net_disconnect(cl, INVALID_CID, "gossip process completed");
}

#endif // KER

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t ker_pack_ack(char* _buffer, uint16_t _size, bool _isGossip, uint16_t _memNumber)
{
    uint32_t pos = 0;
    cx_binw_bool(_buffer, _size, &pos, _isGossip);
    if (_isGossip)
    {
        cx_binw_uint16(_buffer, _size, &pos, _memNumber);
    }
    return pos;
}

uint32_t ker_pack_req_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval)
{
    return common_pack_req_create(_buffer, _size, _remoteId, _tableName, _consistency, _numPartitions, _compactionInterval);
}

uint32_t ker_pack_req_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    return common_pack_req_drop(_buffer, _size, _remoteId, _tableName);
}

uint32_t ker_pack_req_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    return common_pack_req_describe(_buffer, _size, _remoteId, _tableName);
}

uint32_t ker_pack_req_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key)
{
    return common_pack_req_select(_buffer, _size, _remoteId, _tableName, _key);
}

uint32_t ker_pack_req_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint64_t _timestamp)
{
    return common_pack_req_insert(_buffer, _size, _remoteId, _tableName, _key, _value, _timestamp);
}

uint32_t ker_pack_req_journal(char* _buffer, uint16_t _size, uint16_t _remoteId)
{
    uint32_t pos = 0;
    common_pack_remote_id(_buffer, _size, &pos, _remoteId);
    return pos;
}

uint32_t ker_pack_req_run(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _lqlFilePath, const char* _logPath)
{
    uint32_t pos = 0;
    common_pack_remote_id(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _lqlFilePath);
    cx_binw_str(_buffer, _size, &pos, _logPath);
    return pos;
}

uint32_t ker_pack_req_addmem(char* _buffer, uint16_t _size, uint16_t _remoteId, uint16_t _memNumber, uint8_t _consistency)
{
    uint32_t pos = 0;
    common_pack_remote_id(_buffer, _size, &pos, _remoteId);
    cx_binw_uint16(_buffer, _size, &pos, _memNumber);
    cx_binw_uint8(_buffer, _size, &pos, _consistency);
    return pos;
}

uint32_t ker_pack_res_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err)
{
    return common_pack_res_create(_buffer, _size, _remoteId, _err);
}

uint32_t ker_pack_res_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err)
{
    return common_pack_res_drop(_buffer, _size, _remoteId, _err);
}

uint32_t ker_pack_res_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err, const table_record_t* _record)
{
    return common_pack_res_select(_buffer, _size, _remoteId, _err, _record);
}

uint32_t ker_pack_res_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err)
{
    return common_pack_res_insert(_buffer, _size, _remoteId, _err);
}
