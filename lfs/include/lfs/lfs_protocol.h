#ifndef LFS_PROTOCOL_H_
#define LFS_PROTOCOL_H_

#include <cx/net.h>

/****************************************************************************************
 ***  MESSAGE HEADERS
 ***************************************************************************************/

typedef enum
{
    LFSP_NONE = 0,
    LFSP_REQ_CREATE,
    LFSP_REQ_DROP,
    LFSP_REQ_DESCRIBE,
    LFSP_REQ_SELECT,
    LFSP_REQ_INSERT,
} LFS_PACKET_HEADERS;

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef LFS

#include "../../src/lfs.h"

void lfs_handle_req_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void lfs_handle_req_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void lfs_handle_req_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void lfs_handle_req_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void lfs_handle_req_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

#endif // LFS

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t lfs_pack_req_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval);

uint32_t lfs_pack_req_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t lfs_pack_req_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t lfs_pack_req_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key);

uint32_t lfs_pack_req_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint32_t _timestamp);

#endif // LFS_PROTOCOL_H_