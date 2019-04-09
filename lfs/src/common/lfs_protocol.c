#include <lfs/lfs_protocol.h>
#include <mem/mem_protocol.h>

#include <cx/binr.h>
#include <cx/binw.h>

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef LFS

#include "../lfs.h"

void lfs_handle_sum_request(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size)
{
    cx_net_ctx_sv_t* svCtx = _common;
    cx_net_client_t* client = _passThrou;
    uint32_t pos = 0;

    // read the message from MEM
    int32_t a = 0;
    int32_t b = 0;
    cx_binr_int32(_data, _size, &pos, &a);
    cx_binr_int32(_data, _size, &pos, &b);

    // perform the operation
    int32_t result = a + b;

    // build our message (pack the message with our response which is the result of the sum operation)
    pos = mem_pack_sum_result(g_buffer, sizeof(g_buffer), result);

    // send the response back to MEM
    cx_net_send(svCtx, MEMP_SUM_RESULT, g_buffer, pos, client->handle);
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