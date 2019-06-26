#include <ker/common_protocol.h>
#include <lfs/lfs_protocol.h>
#include <mem/mem_protocol.h>
#include <ker/defines.h>
#include <ker/taskman.h>

#include <cx/binr.h>
#include <cx/binw.h>
#include <cx/halloc.h>
#include <cx/mem.h>
#include <cx/str.h>
#include <cx/reslock.h>

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef LFS

#include "../lfs.h"

void lfs_handle_auth(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    cx_net_ctx_sv_t* sv = (cx_net_ctx_sv_t*)_common;
    cx_net_client_t* client = (cx_net_client_t*)_userData;
    uint32_t pos = 0;

    password_t passwd;
    cx_binr_str(_buffer, _bufferSize, &pos, passwd, sizeof(passwd));

    if (0 == strncmp(g_ctx.cfg.password, passwd, MAX_PASSWD_LEN))
    {
        cx_net_validate(_common, client->handle);
        payload_t payload;
        uint32_t payloadSize = mem_pack_ack(payload, sizeof(payload), g_ctx.cfg.valueSize);
        cx_net_send(sv, MEMP_ACK, payload, payloadSize, client->handle);
    }
    else
    {
        cx_net_disconnect(_common, client->handle, "authentication failed");
    }
}

void lfs_handle_req_create(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_CREATE);
    {
        task->data = common_unpack_req_create(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void lfs_handle_req_drop(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_DROP);
    {
        task->data = common_unpack_req_drop(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void lfs_handle_req_describe(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_DESCRIBE);
    {
        task->data = common_unpack_req_describe(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void lfs_handle_req_select(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_SELECT);
    {
        task->data = common_unpack_req_select(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

void lfs_handle_req_insert(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_INSERT);
    {
        task->data = common_unpack_req_insert(_buffer, _bufferSize, &bufferPos, NULL);
    }
    REQ_END;
}

#endif // LFS

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t lfs_pack_auth(char* _buffer, uint16_t _size, password_t _passwd)
{
    uint32_t pos = 0;
    cx_binw_str(_buffer, _size, &pos, _passwd);
    return pos;
}

uint32_t lfs_pack_req_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval)
{
    return common_pack_req_create(_buffer, _size, _remoteId, _tableName, _consistency, _numPartitions, _compactionInterval);
}

uint32_t lfs_pack_req_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    return common_pack_req_drop(_buffer, _size, _remoteId, _tableName);
}

uint32_t lfs_pack_req_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    return common_pack_req_describe(_buffer, _size, _remoteId, _tableName);
}

uint32_t lfs_pack_req_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key)
{
    return common_pack_req_select(_buffer, _size, _remoteId, _tableName, _key);
}

uint32_t lfs_pack_req_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint64_t _timestamp)
{
    return common_pack_req_insert(_buffer, _size, _remoteId, _tableName, _key, _value, _timestamp);
}
