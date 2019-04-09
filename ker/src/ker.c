#include "ker.h"

#include <cx/cx.h>
#include <cx/mem.h>
#include <cx/file.h>
#include <cx/net.h>
#include <cx/str.h>

#include <mem/mem_protocol.h>
#include <ker/ker_protocol.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

cx_net_ctx_cl_t* g_memConn;     // client context to connect to MEM node server
cx_net_ctx_sv_t* g_sv;          // server context for serving API requests coming from clients?
                                //TODO do we actually need this?
char g_buffer[MAX_PACKET_LEN - MIN_PACKET_LEN];

int main(int _argc, char** _argv)
{
    cx_init(PROJECT_NAME);
    
    cx_net_args_t memConnArgs;
    CX_MEM_ZERO(memConnArgs);
    cx_str_copy(memConnArgs.name, sizeof(memConnArgs.name), "mem-1");
    cx_str_copy(memConnArgs.ip, sizeof(memConnArgs.ip), "127.0.0.1");
    memConnArgs.port = 15501;    
    memConnArgs.msgHandlers[KERP_SUM_COMPLETE] = ker_handle_sum_complete;
    g_memConn = cx_net_connect(&memConnArgs);
    
    bool requestedSum = false;

    for (uint32_t i = 0; i < 15; i++)
    {
        sleep(1);
        cx_net_poll_events(g_memConn);

        // wait until we're connected to start asking sums!
        if (!requestedSum && (CX_NET_STATE_CONNECTED & g_memConn->c.state))
        {
            // send a sum request to the MEM node 
            requestedSum = true;

            // pack our sum request packet
            uint32_t size = mem_pack_sum_request(g_buffer, sizeof(g_buffer), 24, 42);

            // send it
            cx_net_send(g_memConn, MEMP_SUM_REQUEST, g_buffer, size, INVALID_HANDLE);

            // console output
            printf("Requesting sum operation: %d + %d\n", 24, 42);
        }
    }

    cx_net_close(g_memConn);
    return 0;
}
