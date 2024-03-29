#ifndef MEM_PROTOCOL_H_
#define MEM_PROTOCOL_H_

#include <ker/defines.h>
#include <cx/net.h>

/****************************************************************************************
 ***  MESSAGE HEADERS
 ***************************************************************************************/

typedef enum
{
    MEMP_NONE = _CX_NETP_BEGIN_,
    MEMP_AUTH,
    MEMP_ACK,
    MEMP_JOURNAL,
    MEMP_REQ_CREATE,
    MEMP_REQ_DROP,
    MEMP_REQ_DESCRIBE,
    MEMP_REQ_SELECT,
    MEMP_REQ_INSERT,
    MEMP_REQ_GOSSIP,
    MEMP_RES_CREATE,
    MEMP_RES_DROP,
    MEMP_RES_DESCRIBE,
    MEMP_RES_SELECT,
    MEMP_RES_INSERT,
    MEMP_RES_GOSSIP,
} MEM_PACKET_HEADERS;

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef MEM

#include "../../src/mem.h"

void mem_handle_auth(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_ack(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_journal(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_req_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_req_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_req_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_req_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_req_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_req_gossip(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_res_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_res_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_res_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_res_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_res_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void mem_handle_res_gossip(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

#endif // MEM

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t mem_pack_auth(char* _buffer, uint16_t _size, password_t _passwd, bool _isGossip, uint16_t _memNumber, uint16_t _memPortNumber);

uint32_t mem_pack_ack(char* _buffer, uint16_t _size, bool _isGossip, uint16_t _memNumber, uint16_t _valueSize);

uint32_t mem_pack_journal(char* _buffer, uint16_t _size);

uint32_t mem_pack_req_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval);

uint32_t mem_pack_req_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t mem_pack_req_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t mem_pack_req_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key);

uint32_t mem_pack_req_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint64_t _timestamp);

uint32_t mem_pack_res_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err);

uint32_t mem_pack_res_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err);

uint32_t mem_pack_res_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err, const table_record_t* _record);

uint32_t mem_pack_res_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err);

#endif // MEM_PROTOCOL_H_
