#include "lfs.h"

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/file.h>
#include <cx/str.h>

#include <lfs/lfs_protocol.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

cx_net_ctx_sv_t* g_sv;          // server context for serving API requests coming from MEM nodes
char g_buffer[MAX_PACKET_LEN - MIN_PACKET_LEN];

int main(int _argc, char** _argv)
{
    cx_init(PROJECT_NAME);
    
    cx_net_args_t svCtxArgs;
    CX_MEM_ZERO(svCtxArgs);
    cx_str_copy(svCtxArgs.name, sizeof(svCtxArgs.name), "api");
    cx_str_copy(svCtxArgs.ip, sizeof(svCtxArgs.ip), "127.0.0.1");
    svCtxArgs.port = 15600;
    svCtxArgs.msgHandlers[LFSP_SUM_REQUEST] = lfs_handle_sum_request;
    
    g_sv = cx_net_listen(&svCtxArgs);
    while (1)
    {
        sleep(1);
        cx_net_poll_events(g_sv);
    }

    cx_net_close(g_sv);
    return 0;
}
