#include <ker/ker_protocol.h>
#include <mem/mem_protocol.h>
#include <lfs/lfs_protocol.h>

#include <cx/binr.h>
#include <cx/binw.h>

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef MEM

void mem_handle_sum_request(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size)
{
    cx_net_ctx_sv_t* svCtx = _common;
    cx_net_client_t* client = _passThrou;
    uint32_t pos = 0;

    // read the message from KER
    int32_t a = 0;
    int32_t b = 0;
    cx_binr_int32(_data, _size, &pos, &a);
    cx_binr_int32(_data, _size, &pos, &b);

    // build our message (pack the packet with the arguments for the sum operation)
    pos = lfs_pack_sum_request(g_buffer, sizeof(g_buffer), a, b);

    // forward the request to LFS
    cx_net_send(g_lfsConn, LFSP_SUM_REQUEST, g_buffer, pos, INVALID_HANDLE);
}

void mem_handle_sum_result(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size)
{
    cx_net_ctx_cl_t* clCtx = _common;
    cx_net_client_t* client = _passThrou;
    uint32_t pos = 0;

    // handle the message from LFS (it contains the result of our sum operation)
    int32_t result = 0;
    cx_binr_int32(_data, _size, &pos, &result);

    // build our message to send the result to KER (the requester of the sum op)
    pos = ker_pack_sum_complete(g_buffer, sizeof(g_buffer), result);

    // please note that this function handles a message coming from LFS to MEM
    // we now need to send it from MEM to KER, so, we need to use g_api server context
    // which holds a connection with KER (the requester of the sum op)

    // since this is a server context which supports multiple clients, we need to specify
    // the _clientHandle parameter, to let the lib know who is the receiver of the message 
    //
    // we'll use clientHandle = 0 just for simplicity (since we'll only have 1 kernel connected for now)
    // but in the future we'll have to keep track of which client sent us the request to be able 
    // to send out reply back to him once we have it
    cx_net_send(g_api, KERP_SUM_COMPLETE, g_buffer, pos, 0);
}

#endif // MEM

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t mem_pack_sum_request(char* _buffer, uint16_t _size, int32_t _a, int32_t _b)
{
    uint32_t pos = 0;
    cx_binw_int32(_buffer, _size, &pos, _a);
    cx_binw_int32(_buffer, _size, &pos, _b);
    return pos;
}

uint32_t mem_pack_sum_result(char* _buffer, uint16_t _size, int32_t _result)
{
    uint32_t pos = 0;
    cx_binw_int32(_buffer, _size, &pos, _result);
    return pos;
}