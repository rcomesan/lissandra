#include <ker/common_protocol.h>
#include <mem/mem_protocol.h>
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

void mem_handle_auth(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    cx_net_ctx_sv_t* sv = (cx_net_ctx_sv_t*)_common;
    cx_net_client_t* client = (cx_net_client_t*)_userData;
    uint32_t pos = 0;

    password_t passwd;
    cx_binr_str(_buffer, _bufferSize, &pos, passwd, sizeof(passwd));

    if (0 == strncmp(g_ctx.cfg.password, passwd, MAX_PASSWD_LEN))
    {
        cx_net_validate(_common, client->handle);

        bool isMemory = false;
        cx_binr_bool(_buffer, _bufferSize, &pos, &isMemory);

        if (isMemory)
        {
            uint16_t memNumber = 0;
            cx_binr_uint16(_buffer, _bufferSize, &pos, &memNumber);
        }
    }
    else
    {
        cx_net_disconnect(_common, client->handle, "authentication failed");
    }
}

void mem_handle_ack(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    cx_net_ctx_cl_t* cl = (cx_net_ctx_cl_t*)_common;
    uint32_t pos = 0;

    cx_binr_uint16(_buffer, _bufferSize, &pos, &g_ctx.cfg.valueSize);

    cx_net_validate(cl, INVALID_HANDLE);
    g_ctx.lfsAvail = true;
    g_ctx.lfsHandshaking = false;
}

void mem_handle_req_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_CREATE);
    {
        task->data = common_unpack_req_create(_buffer, _bufferSize, &bufferPos, &task->remoteId);
    }
    REQ_END;
}

void mem_handle_req_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_DROP);
    {
        task->data = common_unpack_req_drop(_buffer, _bufferSize, &bufferPos, &task->remoteId);
    }
    REQ_END;
}

void mem_handle_req_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_DESCRIBE);
    {
        task->data = common_unpack_req_describe(_buffer, _bufferSize, &bufferPos, &task->remoteId);
    }
    REQ_END;
}

void mem_handle_req_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_SELECT);
    {
        task->data = common_unpack_req_select(_buffer, _bufferSize, &bufferPos, &task->remoteId);
    }
    REQ_END;
}

void mem_handle_req_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_INSERT);
    {
        task->data = common_unpack_req_insert(_buffer, _bufferSize, &bufferPos, &task->remoteId);
    }
    REQ_END;
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

    if (ERR_NONE != err.code)
    {
        CX_INFO("memory journal: %s", err.desc);
    }
}

#endif // MEM

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t mem_pack_auth(char* _buffer, uint16_t _size, password_t _passwd, uint16_t _memNumber)
{
    uint32_t pos = 0;

    cx_binw_str(_buffer, _size, &pos, _passwd);

    bool isMemory = _memNumber > 0;
    cx_binw_bool(_buffer, _size, &pos, isMemory);

    if (isMemory)
    {
        cx_binw_uint16(_buffer, _size, &pos, _memNumber);
    }

    return pos;
}

uint32_t mem_pack_ack(char* _buffer, uint16_t _size, uint16_t _valueSize)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _valueSize);
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

uint32_t mem_pack_req_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint32_t _timestamp)
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
