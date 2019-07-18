#ifndef COMMON_PROTOCOL_H_
#define COMMON_PROTOCOL_H_

#include <ker/taskman.h>
#include <cx/net.h>

#define REQ_BEGIN(_taskType)                                                            \
    cx_net_ctx_sv_t* svCtx = (cx_net_ctx_sv_t*)_common;                                 \
    cx_net_client_t* client = (cx_net_client_t*)_userData;                              \
    uint32_t bufferPos = 0;                                                             \
                                                                                        \
    uint16_t remoteId = INVALID_HANDLE;                                                 \
    cx_binr_uint16(_buffer, _bufferSize, &bufferPos, &remoteId);                        \
                                                                                        \
    task_t* task = NULL;                                                                \
    if (NULL == client)                                                                 \
        task = taskman_create(TASK_ORIGIN_CLI, (_taskType), NULL, INVALID_CID);         \
    else                                                                                \
        task = taskman_create(TASK_ORIGIN_API, (_taskType), NULL, client->cid.id);      \
                                                                                        \
    if (NULL != task)                                                                   \
    {

#define REQ_END                                                                         \
        task->remoteId = remoteId;                                                      \
        task->state = TASK_STATE_NEW;                                                   \
        CX_CHECK(bufferPos == _bufferSize, "%d bytes were not consumed from the buffer!", _bufferSize - bufferPos); \
    }


#define RES_BEGIN                                                                       \
    cx_net_ctx_cl_t* clCtx = (cx_net_ctx_cl_t*)_common;                                 \
    bool complete = true;                                                               \
    uint32_t bufferPos = 0;                                                             \
    uint16_t remoteId = INVALID_HANDLE;                                                 \
    cx_binr_uint16(_buffer, _bufferSize, &bufferPos, &remoteId);                        \
    task_t* task = taskman_get(remoteId);                                               \
    if (NULL != task)                                                                   \
    {

#define RES_END                                                                         \
        if (complete)                                                                   \
        {                                                                               \
            pthread_mutex_lock(&task->responseMtx);                                     \
            task->state = TASK_STATE_RUNNING;                                           \
            pthread_cond_signal(&task->responseCond);                                   \
            pthread_mutex_unlock(&task->responseMtx);                                   \
        }                                                                               \
    }                                                                                   \
    CX_CHECK(NULL != task, "invalid response for task handle %d!", remoteId);

/****************************************************************************************
 ***  COMMON MESSAGE PACKERS
 ***************************************************************************************/

//TODO refactor pack_req methods to receive a pointer to a buffer position (uint32_t) and return void

uint32_t            common_pack_req_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval);

uint32_t            common_pack_req_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t            common_pack_req_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t            common_pack_req_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key);

uint32_t            common_pack_req_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint64_t _timestamp);

uint32_t            common_pack_res_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err);

uint32_t            common_pack_res_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err);

bool                common_pack_res_describe(char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t _remoteId, table_meta_t* _tables, uint16_t _tablesCount, uint16_t* _tablesPacked, const cx_err_t* _err);

uint32_t            common_pack_res_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err, const table_record_t* _record);

uint32_t            common_pack_res_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err);

uint32_t            common_pack_table_meta(char* _buffer, uint16_t _size, const table_meta_t* _table);

uint32_t            common_pack_table_record(char* _buffer, uint16_t _size, const table_record_t* _record);

void                common_pack_remote_id(char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t _remoteId);

void                common_unpack_remote_id(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId);

/****************************************************************************************
 ***  COMMON MESSAGE UNPACKERS
 ***************************************************************************************/

data_create_t*      common_unpack_req_create(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId);

data_drop_t*        common_unpack_req_drop(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId);

data_describe_t*    common_unpack_req_describe(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId);

data_select_t*      common_unpack_req_select(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId);

data_insert_t*      common_unpack_req_insert(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId);

void                common_unpack_res_create(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, cx_err_t* _err);

void                common_unpack_res_drop(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, cx_err_t* _err);

void                common_unpack_res_describe(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, data_describe_t* _outData, cx_err_t* _err);

void                common_unpack_res_select(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, data_select_t* _outData, cx_err_t* _err);

void                common_unpack_res_insert(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, cx_err_t* _err);

void                common_unpack_table_meta(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, table_meta_t* _outTable);

void                common_unpack_table_record(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, table_record_t* _outRecord);

#endif // COMMON_PROTOCOL_H_