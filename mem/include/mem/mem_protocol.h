#ifndef MEM_PROTOCOL_H_
#define MEM_PROTOCOL_H_

#include <cx/net.h>

/****************************************************************************************
 ***  MESSAGE HEADERS
 ***************************************************************************************/

typedef enum
{
    MEMP_SUM_REQUEST = 0,
    MEMP_CREATE,
    MEMP_DROP,
    MEMP_DESCRIBE,
    MEMP_SELECT,
    MEMP_INSERT,
} MEM_PACKET_HEADERS;

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef MEM

#include "../../src/mem.h"

void mem_handle_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

#endif // MEM

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t mem_pack_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval);

uint32_t mem_pack_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t mem_pack_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t mem_pack_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key);

uint32_t mem_pack_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint32_t _timestamp);

#endif // MEM_PROTOCOL_H_
