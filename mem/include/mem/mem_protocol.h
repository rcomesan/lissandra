#ifndef MEM_PROTOCOL_H_
#define MEM_PROTOCOL_H_

#include <cx/net.h>

/****************************************************************************************
 ***  MESSAGE HEADERS
 ***************************************************************************************/

typedef enum
{
    MEMP_SUM_REQUEST = 0,
    MEMP_SUM_RESULT,
} MEM_PACKET_HEADERS;

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef MEM

#include "../../src/mem.h"

void mem_handle_sum_request(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size);

void mem_handle_sum_result(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size);

#endif // MEM

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t mem_pack_sum_result(char* _buffer, uint16_t _size, int32_t _result);

uint32_t mem_pack_sum_request(char* _buffer, uint16_t _size, int32_t _a, int32_t _b);

#endif // MEM_PROTOCOL_H_
