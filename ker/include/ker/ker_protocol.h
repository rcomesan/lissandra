#ifndef KER_PROTOCOL_H_
#define KER_PROTOCOL_H_

#include <ker/defines.h>
#include <cx/net.h>

/****************************************************************************************
 ***  MESSAGE HEADERS
 ***************************************************************************************/

typedef enum
{
    KERP_NONE = _CX_NETP_BEGIN_,
    KERP_AUTH,
    KERP_ACK,
    KERP_RES_CREATE,
    KERP_RES_DROP,
    KERP_RES_DESCRIBE,
    KERP_RES_SELECT,
    KERP_RES_INSERT,
} KER_PACKET_HEADERS;

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef KER

#include "../../src/ker.h"

void ker_handle_ack(cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_req_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_req_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_req_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_req_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_req_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_req_journal(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_req_addmem(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_req_run(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_res_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_res_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_res_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_res_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

void ker_handle_res_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize);

#endif // KER

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t ker_pack_ack(char* _buffer, uint16_t _size);

uint32_t ker_pack_req_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval);

uint32_t ker_pack_req_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t ker_pack_req_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName);

uint32_t ker_pack_req_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key);

uint32_t ker_pack_req_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint32_t _timestamp);

uint32_t ker_pack_req_journal(char* _buffer, uint16_t _size, uint16_t _remoteId);

uint32_t ker_pack_req_run(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _lqlFilePath);

uint32_t ker_pack_req_addmem(char* _buffer, uint16_t _size, uint16_t _remoteId, uint16_t _memNumber, uint8_t _consistency);

uint32_t ker_pack_res_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err);

uint32_t ker_pack_res_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err);

uint32_t ker_pack_res_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err, const table_record_t* _record);

uint32_t ker_pack_res_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err);

#endif // KER_PROTOCOL_H_