#include <lfs/lfs_protocol.h>
#include <mem/mem_protocol.h>
#include <ker/defines.h>

#include <cx/binr.h>
#include <cx/binw.h>
#include <cx/halloc.h>
#include <cx/mem.h>
#include <cx/str.h>

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef LFS

#include "../lfs.h"

#define LFS_REQ_BEGIN                                                                   \
    cx_net_ctx_sv_t* svCtx = (cx_net_ctx_sv_t*)_common;                                 \
    cx_net_client_t* client = (cx_net_client_t*)_passThrou;                             \
    uint32_t pos = 0;                                                                   \
    uint16_t requestHandle = cx_handle_alloc(g_ctx.requestsHalloc);                     \
    if (INVALID_HANDLE != requestHandle)                                                \
    {                                                                                   \
        request_t* req = &(g_ctx.requests[requestHandle]);                              \
        req->state = REQ_STATE_NEW;                                                     \
        if (NULL != _passThrou)                                                         \
        {                                                                               \
            req->origin = REQ_ORIGIN_API;                                               \
            req->clientHandle = client->handle;                                         \
        }                                                                               \
        else                                                                            \
        {                                                                               \
            req->origin = REQ_ORIGIN_CLI;                                               \
            req->clientHandle = INVALID_HANDLE;                                         \
        }

#define LFS_REQ_END                                                                     \
        CX_CHECK(pos == _size, "%d bytes were not consumed from the buffer!", _size - pos); \
    }                                                                                   \
    CX_WARN(INVALID_HANDLE != requestHandle, "we ran out of request handles! (request ignored)");

void lfs_handle_sum_request(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size)
{
    //LFS_REQ_BEGIN;

    //// read the message from MEM
    //int32_t a = 0;
    //int32_t b = 0;
    //cx_binr_int32(_data, _size, &pos, &a);
    //cx_binr_int32(_data, _size, &pos, &b);

    //// perform the operation
    //int32_t result = a + b;

    //// build our message (pack the message with our response which is the result of the sum operation)
    //pos = mem_pack_sum_result(g_ctx.buffer, sizeof(g_ctx.buffer), result);

    //// send the response back to MEM
    //cx_net_send(svCtx, MEMP_SUM_RESULT, g_ctx.buffer, pos, client->handle);

    //LFS_REQ_END;
}

void lfs_handle_create(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size)
{
    LFS_REQ_BEGIN;
        data_create_t* data = CX_MEM_STRUCT_ALLOC(data);
        req->type = REQ_TYPE_CREATE;
        req->data = data;
        cx_binr_uint16(_data, _size, &pos, &data->c.remoteId);
        cx_binr_str(_data, _size, &pos, data->tableName, sizeof(data->tableName));
        cx_binr_uint8(_data, _size, &pos, &data->consistency);
        cx_binr_uint16(_data, _size, &pos, &data->numPartitions);
        cx_binr_uint32(_data, _size, &pos, &data->compactionInterval);
    LFS_REQ_END;
}

void lfs_handle_drop(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size)
{
    LFS_REQ_BEGIN;
        data_drop_t* data = CX_MEM_STRUCT_ALLOC(data);
        req->type = REQ_TYPE_DROP;
        req->data = data;
        cx_binr_uint16(_data, _size, &pos, &data->c.remoteId);
        cx_binr_str(_data, _size, &pos, data->tableName, sizeof(data->tableName));
    LFS_REQ_END;
}

void lfs_handle_describe(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size)
{
    LFS_REQ_BEGIN;
        data_describe_t* data = CX_MEM_STRUCT_ALLOC(data);
        req->type = REQ_TYPE_DESCRIBE;
        req->data = data;
        cx_binr_uint16(_data, _size, &pos, &data->c.remoteId);
        
        char tableName[TABLE_NAME_LEN_MAX + 1];
        cx_binr_str(_data, _size, &pos, tableName, sizeof(tableName));

        if (cx_str_is_empty(tableName))
        {
            data->tablesCount = (uint16_t)cx_cdict_size(g_ctx.tables);
            data->tables = CX_MEM_ARR_ALLOC(data->tables, data->tablesCount);

            //TODO iterate the dictionary somehow
        }
        else
        {
            table_t* table = NULL;
            
            if (cx_cdict_get(g_ctx.tables, tableName, (void**)&table))
            {
                data->tablesCount = 0;
                data->tables = CX_MEM_STRUCT_ALLOC(data->tables);
                memcpy(&data->tables[0], &table->meta, sizeof(table->meta));
            }
            //TODO table does not exist
        }
    LFS_REQ_END;
}

void lfs_handle_select(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size)
{
    LFS_REQ_BEGIN;
        data_select_t* data = CX_MEM_STRUCT_ALLOC(data);
        req->type = REQ_TYPE_SELECT;
        req->data = data;
        cx_binr_uint16(_data, _size, &pos, &data->c.remoteId);
        cx_binr_str(_data, _size, &pos, data->tableName, sizeof(data->tableName));
        cx_binr_uint16(_data, _size, &pos, &data->key);
    LFS_REQ_END;
}

void lfs_handle_insert(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size)
{
    LFS_REQ_BEGIN;
        data_insert_t* data = CX_MEM_STRUCT_ALLOC(data);
        req->type = REQ_TYPE_INSERT;
        req->data = data;
        cx_binr_uint16(_data, _size, &pos, &data->c.remoteId);
        cx_binr_str(_data, _size, &pos, data->tableName, sizeof(data->tableName));
        cx_binr_uint16(_data, _size, &pos, &data->key);

        uint16_t valueLen = cx_binr_str(_data, _size, &pos, NULL, 0) + 1;
        data->value = malloc(valueLen);
        cx_binr_str(_data, _size, &pos, data->value, valueLen);

        cx_binr_uint32(_data, _size, &pos, &data->timestamp);
    LFS_REQ_END;
}

#endif // LFS

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t lfs_pack_sum_request(char* _buffer, uint16_t _size, int32_t _a, int32_t _b)
{
    uint32_t pos = 0;
    cx_binw_int32(_buffer, _size, &pos, _a);
    cx_binw_int32(_buffer, _size, &pos, _b);
    return pos;
}

uint32_t lfs_pack_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    cx_binw_uint8(_buffer, _size, &pos, _consistency);
    cx_binw_uint16(_buffer, _size, &pos, _numPartitions);
    cx_binw_uint32(_buffer, _size, &pos, _compactionInterval);
    return pos;
}

uint32_t lfs_pack_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    return pos;
}

uint32_t lfs_pack_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, (NULL != _tableName) ? _tableName : "");
    return pos;
}

uint32_t lfs_pack_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    cx_binw_uint16(_buffer, _size, &pos, _key);
    return pos;
}

uint32_t lfs_pack_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint32_t _timestamp)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    cx_binw_uint16(_buffer, _size, &pos, _key);
    cx_binw_str(_buffer, _size, &pos, _value);
    cx_binw_uint32(_buffer, _size, &pos, _timestamp);
    return pos;
}
