#ifndef LFS_PROTOCOL_H_
#define LFS_PROTOCOL_H_

#include <cx/net.h>

/****************************************************************************************
 ***  MESSAGE HEADERS
 ***************************************************************************************/

typedef enum
{
    LFSP_SUM_REQUEST = 0,
    LFSP_CREATE,
    LFSP_DROP,
    LFSP_DESCRIBE,
    LFSP_SELECT,
    LFSP_INSERT,   
} LFS_PACKET_HEADERS;

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef LFS

#include "../../src/lfs.h"

void lfs_handle_sum_request(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size);

void lfs_handle_create(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size);

void lfs_handle_drop(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size);

void lfs_handle_describe(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size);

void lfs_handle_select(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size);

void lfs_handle_insert(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size);

#endif // LFS

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t lfs_pack_sum_request(char* _buffer, uint16_t _size, int32_t _a, int32_t _b);

uint32_t lfs_pack_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval);

uint32_t lfs_pack_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t lfs_pack_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t lfs_pack_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key);

uint32_t lfs_pack_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint32_t _timestamp);

#endif // LFS_PROTOCOL_H_