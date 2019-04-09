#include <ker/ker_protocol.h>
#include <mem/mem_protocol.h>

#include <cx/binr.h>
#include <cx/binw.h>
#include <stdio.h>

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef KER

void ker_handle_sum_complete(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size)
{
    cx_net_ctx_cl_t* clCtx = (cx_net_ctx_cl_t*)_common;
    uint32_t pos = 0;

    // read the message from MEM
    int32_t result = 0;
    cx_binr_int32(_data, _size, &pos, &result);

    printf("LFS says the sum is: %d\n", result);
}

#endif // KER

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t ker_pack_sum_complete(char* _buffer, uint16_t _size, int32_t _result)
{
    uint32_t pos = 0;
    cx_binw_int32(_buffer, _size, &pos, _result);
    return pos;
}