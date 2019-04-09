#ifndef LFS_H_
#define LFS_H_

#include <cx/net.h>

extern cx_net_ctx_sv_t* g_sv;          // server context for serving API requests coming from MEM nodes
extern char g_buffer[MAX_PACKET_LEN - MIN_PACKET_LEN];

#endif // LFS_H_
