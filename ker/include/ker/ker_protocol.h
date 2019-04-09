#ifndef KER_PROTOCOL_H_
#define KER_PROTOCOL_H_

#include <cx/net.h>

/****************************************************************************************
 ***  MESSAGE HEADERS
 ***************************************************************************************/

typedef enum
{
    KERP_SUM_COMPLETE = 0,
} KER_PACKET_HEADERS;

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef KER

#include "../../src/ker.h"

extern cx_net_ctx_cl_t* g_memConn;      // client context to connect to MEM node server
extern cx_net_ctx_sv_t* g_sv;           // server context for serving API requests coming from clients?
                                        //TODO do we actually need this?
extern char g_buffer[MAX_PACKET_LEN - MIN_PACKET_LEN];

void ker_handle_sum_complete(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size);

#endif // KER

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t ker_pack_sum_complete(char* _buffer, uint16_t _size, int32_t _result);

#endif // KER_PROTOCOL_H_