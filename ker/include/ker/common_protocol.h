#ifndef COMMON_PROTOCOL_H_
#define COMMON_PROTOCOL_H_

#include <ker/taskman.h>
#include <cx/net.h>

#define REQ_BEGIN(_taskType)                                                            \
    cx_net_ctx_sv_t* svCtx = (cx_net_ctx_sv_t*)_common;                                 \
    cx_net_client_t* client = (cx_net_client_t*)_userData;                              \
    uint32_t bufferPos = 0;                                                                   \
    task_t* task = taskman_create(                                                      \
        NULL == _userData                                                               \
            ? TASK_ORIGIN_CLI                                                           \
            : TASK_ORIGIN_API,                                                          \
        (_taskType),                                                                    \
        NULL,                                                                           \
        _userData);                                                                     \
                                                                                        \
    if (NULL != task)                                                                   \
    {

#define REQ_END                                                                         \
        task->state = TASK_STATE_NEW;                                                   \
        CX_CHECK(bufferPos == _bufferSize, "%d bytes were not consumed from the buffer!", _bufferSize - bufferPos); \
    }


/****************************************************************************************
 ***  COMMON MESSAGE PACKERS
 ***************************************************************************************/

uint32_t common_pack_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval);

uint32_t common_pack_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t common_pack_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t common_pack_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key);

uint32_t common_pack_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint32_t _timestamp);

/****************************************************************************************
 ***  COMMON MESSAGE UNPACKERS
 ***************************************************************************************/

data_create_t*      common_unpack_create(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId);

data_drop_t*        common_unpack_drop(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId);

data_describe_t*    common_unpack_describe(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId);

data_select_t*      common_unpack_select(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId);

data_insert_t*      common_unpack_insert(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId);

#endif // COMMON_PROTOCOL_H_