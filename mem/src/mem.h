#ifndef MEM_H_
#define MEM_H_

#include <cx/net.h>

extern cx_net_ctx_sv_t* g_api;       // server context for serving API requests from KER nodes
extern cx_net_ctx_cl_t* g_lfsConn;  // client context for sending requests to the LFS node
extern char g_buffer[MAX_PACKET_LEN - MIN_PACKET_LEN];

#endif // MEM_H_