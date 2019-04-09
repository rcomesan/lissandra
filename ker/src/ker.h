#ifndef KER_H_
#define KER_H_

#include <cx/net.h>

extern cx_net_ctx_cl_t* g_memConn;     // client context to connect to MEM node server
extern cx_net_ctx_sv_t* g_sv;          // server context for serving API requests coming from clients?
                                //TODO do we actually need this?
extern char g_buffer[MAX_PACKET_LEN - MIN_PACKET_LEN];

#endif // KER_H_
