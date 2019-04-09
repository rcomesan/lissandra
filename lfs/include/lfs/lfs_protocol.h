#ifndef LFS_PROTOCOL_H_
#define LFS_PROTOCOL_H_

#include <cx/net.h>

/****************************************************************************************
 ***  MESSAGE HEADERS
 ***************************************************************************************/

typedef enum
{
    LFSP_SUM_REQUEST = 0,
} LFS_PACKET_HEADERS;

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef LFS

#include "../../src/lfs.h"

void lfs_handle_sum_request(const cx_net_common_t* _common, void* _passThrou, const char* _data, uint16_t _size);

#endif // LFS

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t lfs_pack_sum_request(char* _buffer, uint16_t _size, int32_t _a, int32_t _b);

#endif // LFS_PROTOCOL_H_