#include "mem.h"

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/file.h>
#include <cx/net.h>
#include <cx/str.h>

#include <mem/mem_protocol.h>
#include <lfs/lfs_protocol.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

cx_net_ctx_sv_t* g_api;       // server context for serving API requests from KER nodes
cx_net_ctx_cl_t* g_lfsConn;  // client context for sending requests to the LFS node
char g_buffer[MAX_PACKET_LEN - MIN_PACKET_LEN];

int main(int _argc, char** _argv)
{
    cx_init(PROJECT_NAME);
    
    cx_net_args_t apiCtxArgs;
    CX_MEM_ZERO(apiCtxArgs);
    cx_str_copy(apiCtxArgs.name, sizeof(apiCtxArgs.name), "api");
    cx_str_copy(apiCtxArgs.ip, sizeof(apiCtxArgs.ip), "127.0.0.1");
    apiCtxArgs.port = 15501;
    apiCtxArgs.msgHandlers[MEMP_SUM_REQUEST] = mem_handle_sum_request;    
    g_api = cx_net_listen(&apiCtxArgs);

    cx_net_args_t lfsConnArgs;
    CX_MEM_ZERO(lfsConnArgs);
    cx_str_copy(lfsConnArgs.name, sizeof(lfsConnArgs.name), "lfs");
    cx_str_copy(lfsConnArgs.ip, sizeof(lfsConnArgs.ip), "127.0.0.1");
    lfsConnArgs.port = 15600;
    lfsConnArgs.msgHandlers[MEMP_SUM_RESULT] = mem_handle_sum_result;
    g_lfsConn = cx_net_connect(&lfsConnArgs);
    
    while (1)
    {
        sleep(1);
        cx_net_poll_events(g_api);
        cx_net_poll_events(g_lfsConn);
    }

    cx_net_close(g_api);
    cx_net_close(g_lfsConn);
    return 0;
}
