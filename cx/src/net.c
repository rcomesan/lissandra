#include "cx.h"
#include "net.h"
#include "mem.h"
#include "str.h"
#include "binr.h"
#include "binw.h"

#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h> 
#include <errno.h>
#include <sys/epoll.h>
#include <string.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_net_ctx_sv_t* cx_net_listen(cx_net_args_t* _args)
{
    cx_net_ctx_sv_t* ctx = CX_MEM_STRUCT_ALLOC(ctx);
    cx_str_copy(ctx->c.name, sizeof(ctx->c.name), _args->name);
    cx_str_copy(ctx->c.ip, sizeof(ctx->c.ip), _args->ip);
    memcpy(ctx->c.msgHandlers, _args->msgHandlers, sizeof(_args->msgHandlers));
    ctx->c.port = _args->port;
    ctx->c.state = CX_NET_STATE_SERVER | CX_NET_STATE_ERROR;
    ctx->clientsMax = 1000;

    sockaddr_in address;
    if (cx_net_parse_address(ctx->c.ip, ctx->c.port, &address))
    {
        CX_INFO("[sv: %s] creating socket...", ctx->c.name);
        ctx->c.sock = socket(AF_INET, SOCK_STREAM, 0);
        if (INVALID_DESCRIPTOR != ctx->c.sock)
        {
            CX_INFO("[sv: %s] setting socket options...", ctx->c.name);
            if (-1 != setsockopt(ctx->c.sock, SOL_SOCKET, SO_REUSEADDR, &(int32_t){ 1 }, sizeof(int32_t))
                && -1 != fcntl(ctx->c.sock, F_SETFL, fcntl(ctx->c.sock, F_GETFL, 0) | O_NONBLOCK))
            {
                CX_INFO("[sv: %s] binding on port %d...", ctx->c.name, ctx->c.port);
                if (-1 != bind(ctx->c.sock, &address, sizeof(address)))
                {
                    CX_INFO("[sv: %s] listening on %s:%d...", ctx->c.name, ctx->c.ip, ctx->c.port);
                    if (-1 != listen(ctx->c.sock, SOMAXCONN))
                    {
                        CX_INFO("[sv: %s] initializing event pooling...", ctx->c.name);
                        ctx->c.epollDescriptor = epoll_create(ctx->clientsMax);
                        if (INVALID_DESCRIPTOR != ctx->c.epollDescriptor)
                        {
                            ctx->c.epollEvents = CX_MEM_ARR_ALLOC(ctx->c.epollEvents, ctx->clientsMax);
                            ctx->clients = CX_MEM_ARR_ALLOC(ctx->clients, ctx->clientsMax);
                            ctx->clientsHalloc = cx_halloc_init(ctx->clientsMax);

                            // add an entry to the "interest list" of epoll containing our
                            // listening socket file descriptor. the kernel will let us 
                            // know when there's something to read on this socket 
                            // (aka new client connected)
                            epoll_event event;
                            CX_MEM_ZERO(event);
                            event.events = EPOLLIN;
                            event.data.fd = ctx->c.sock;
                            epoll_ctl(ctx->c.epollDescriptor, EPOLL_CTL_ADD, ctx->c.sock, &event);

                            ctx->c.state &= (~CX_NET_STATE_ERROR);
                            ctx->c.state |= CX_NET_STATE_LISTENING;
                            CX_INFO("[sv: %s] started serving on %s:%d", ctx->c.name, ctx->c.ip, ctx->c.port);
                        }
                    }
                }
            }
        }
    }
    
    if (CX_NET_STATE_ERROR & ctx->c.state)
    {
        if (0 != ctx->c.sock)
        {
            close(ctx->c.sock);
            ctx->c.sock = INVALID_DESCRIPTOR;
        }

        if (0 != ctx->c.epollDescriptor)
        {
            close(ctx->c.epollDescriptor);
            ctx->c.epollDescriptor = INVALID_DESCRIPTOR;
        }

        ctx->c.errorNumber = errno;
        CX_WARN(CX_ALW, "[sv: %s] listen failed: %s (%s:%d)", ctx->c.name, strerror(errno), ctx->c.ip, ctx->c.port);
    }

    return ctx;
}

cx_net_ctx_cl_t* cx_net_connect(cx_net_args_t* _args)
{
    cx_net_ctx_cl_t* ctx = CX_MEM_STRUCT_ALLOC(ctx);
    cx_str_copy(ctx->c.name, sizeof(ctx->c.name), _args->name);
    cx_str_copy(ctx->c.ip, sizeof(ctx->c.ip), _args->ip);
    memcpy(ctx->c.msgHandlers, _args->msgHandlers, sizeof(_args->msgHandlers));
    ctx->c.port = _args->port;
    ctx->c.state = CX_NET_STATE_CLIENT | CX_NET_STATE_ERROR;

    epoll_event event;
    CX_MEM_ZERO(event);

    sockaddr_in address;
    if (cx_net_parse_address(ctx->c.ip, ctx->c.port, &address))
    {
        CX_INFO("[cl: %s] creating socket...", ctx->c.name);
        ctx->c.sock = socket(AF_INET, SOCK_STREAM, 0);
        if (-1 != ctx->c.sock)
        {
            CX_INFO("[cl: %s] setting socket options...", ctx->c.name);
            if (-1 != fcntl(ctx->c.sock, F_SETFL, fcntl(ctx->c.sock, F_GETFL, 0) | O_NONBLOCK))
            {
                CX_INFO("[cl: %s] initializing event pooling...", ctx->c.name);
                ctx->c.epollDescriptor = epoll_create(1);
                if (INVALID_DESCRIPTOR != ctx->c.epollDescriptor)
                {
                    event.events = EPOLLIN;
                    event.data.fd = ctx->c.sock;

                    CX_INFO("[cl: %s] connecting to %s:%d...", ctx->c.name, ctx->c.ip, ctx->c.port);
                    if (-1 != connect(ctx->c.sock, &address, sizeof(address)))
                    {
                        ctx->c.state &= (~CX_NET_STATE_ERROR);
                        ctx->c.state |= CX_NET_STATE_CONNECTED;
                        CX_INFO("[cl: %s] connection established to server on %s:%d", ctx->c.name, ctx->c.ip, ctx->c.port);
                    }
                    else if (EINPROGRESS == errno | EAGAIN == errno)
                    {
                        // operation can't be performed right now, but epoll will let us know when it's done
                        event.events |= EPOLLOUT;
                        ctx->c.state &= (~CX_NET_STATE_ERROR);
                        ctx->c.state |= CX_NET_STATE_CONNECTING;
                    }
                }
            }
        }
    }

    if (CX_NET_STATE_ERROR & ctx->c.state)
    {
        if (0 != ctx->c.sock)
        {
            close(ctx->c.sock);
            ctx->c.sock = INVALID_DESCRIPTOR;
        }

        ctx->c.errorNumber = errno;
        CX_WARN(CX_ALW, "[cl: %s] connect failed: %s (%s:%d)", ctx->c.name, strerror(errno), ctx->c.ip, ctx->c.port);
    }
    else
    {
        // register our desired events in the epoll instance
        epoll_ctl(ctx->c.epollDescriptor, EPOLL_CTL_ADD, ctx->c.sock, &event);
        ctx->c.epollEvents = CX_MEM_ARR_ALLOC(ctx->c.epollEvents, 1);
    }

    return ctx;
}

void cx_net_close(void* _ctx)
{
    CX_CHECK_NOT_NULL(_ctx);

    cx_net_ctx_t ctx;
    ctx.c = _ctx;

    if (CX_NET_STATE_SERVER & ctx.c->state)
    {
        cx_net_ctx_sv_t* svCtx = ctx.sv;

        //flush & disconnect all clients
        uint16_t clientsCount = cx_handle_count(svCtx->clientsHalloc);
        uint16_t handle;
        for (uint16_t i = 0; i < clientsCount; i++)
        {
            handle = cx_handle_at(svCtx->clientsHalloc, i);
            cx_net_flush(svCtx, handle);
            close(svCtx->clients[handle].sock);
        }
        
        free(svCtx->clients);
        cx_halloc_destroy(svCtx->clientsHalloc);
        
        CX_INFO("[sv: %s] finished serving on %s:%d", ctx.c->name, ctx.c->ip, ctx.c->port);
    }
    else if (CX_NET_STATE_CLIENT & ctx.c->state)
    {
        cx_net_ctx_cl_t* clCtx = ctx.cl;
        
        cx_net_flush(clCtx, INVALID_HANDLE);

        CX_INFO("[cl: %s] disconnected from server on %s:%d", ctx.c->name, ctx.c->ip, ctx.c->port);
    }

    // destroy epoll instance and free epoll events array
    if (INVALID_DESCRIPTOR != ctx.c->epollDescriptor)
    {
        close(ctx.c->epollDescriptor);
        ctx.c->epollDescriptor = INVALID_DESCRIPTOR;
    }
    free(ctx.c->epollEvents);

    // close the common socket
    if (INVALID_DESCRIPTOR != ctx.c->sock)
    {
        close(ctx.c->sock);
        ctx.c->sock = INVALID_DESCRIPTOR;
    }

    free(_ctx);
}

void cx_net_poll_events(void* _ctx)
{
    CX_CHECK_NOT_NULL(_ctx);

    cx_net_ctx_t ctx;
    ctx.c = _ctx;

    if (CX_NET_STATE_SERVER & ctx.c->state)
    {
        if (CX_NET_STATE_LISTENING & ctx.c->state)
        {
            cx_net_poll_events_server(ctx.sv);
        }
    }
    else if (CX_NET_STATE_CLIENT & ctx.c->state)
    {
        if ((CX_NET_STATE_CONNECTING | CX_NET_STATE_CONNECTED) & ctx.c->state)
        {
            cx_net_poll_events_client(ctx.cl);
        }
    }
}

void cx_net_send(void* _ctx, uint8_t _header, const char* _payload, uint32_t _payloadSize, uint16_t _clientHandle)
{
    CX_CHECK_NOT_NULL(_ctx);
    CX_CHECK(_payloadSize <= MAX_PACKET_LEN - MIN_PACKET_LEN, "_payloadSize can't be greater than %d bytes", MAX_PACKET_LEN - MIN_PACKET_LEN);

    if (_payloadSize <= MAX_PACKET_LEN - MIN_PACKET_LEN)
    {
        cx_net_ctx_t ctx;
        ctx.c = _ctx;

        uint32_t bytesRequired = MIN_PACKET_LEN + _payloadSize;
        int32_t sock = 0;
        char* buffer = NULL;
        uint32_t* position = NULL;

        if (CX_NET_STATE_SERVER & ctx.c->state)
        {
            CX_CHECK(cx_handle_is_valid(ctx.sv->clientsHalloc, _clientHandle),
                "[sv: %s] sending a packet to an invalid client! (handle: %d)", ctx.c->name, _clientHandle);

            sock = ctx.sv->clients[_clientHandle].sock;
            buffer = ctx.sv->clients[_clientHandle].out;
            position = &(ctx.sv->clients[_clientHandle].outPos);
        }
        else if (CX_NET_STATE_CLIENT & ctx.c->state)
        {
            CX_CHECK(CX_NET_STATE_CONNECTED & ctx.c->state, "[cl: %s] this ctx is not connected! you must check CX_NET_STATE_CONNECTED flag before calling send", ctx.c->name);

            sock = ctx.cl->c.sock;
            buffer = ctx.cl->out;
            position = &(ctx.cl->outPos);
        }

        if ((*position) + bytesRequired > CX_NET_BUFLEN)
        {
            // not enough space to add this packet to the outbound buffer
            // try to write it out to the socket and start from byte 0
            if (!cx_net_flush(_ctx, _clientHandle))
            {
                // we do have a serious problem at this point since our outbound
                // buffer can't be written to the socket (due to probably blocking i/o)
                // there isn't much we can do other than using a bigger buffer 
                // we'll ignore this packet for now :(
                if (CX_NET_STATE_SERVER & ctx.c->state)
                {
                    CX_WARN(CX_ALW, "[sv: %s] [handle: %d] we ran out of outbound buffer space to write packet #%d of length %d bytes",
                        ctx.c->name, _clientHandle, _header, _payloadSize);
                }
                else if (CX_NET_STATE_CLIENT & ctx.c->state)
                {
                    CX_WARN(CX_ALW, "[cl: %s] we ran out of outbound buffer space to write packet #%d of length %d bytes", 
                        ctx.c->name, _header, _payloadSize);
                }
                return;
            }
        }

        // write the packet to our buffer
        cx_binw_uint8(buffer, position, _header);
        cx_binw_uint16(buffer, position, _payloadSize);

        if (_payloadSize > 0)
        {
            CX_CHECK_NOT_NULL(_payload);
            memcpy(&(buffer[*position]), _payload, _payloadSize);
            (*position) += _payloadSize;
        }
    }
}

bool cx_net_flush(void* _ctx, uint16_t _clientHandle)
{
    CX_CHECK_NOT_NULL(_ctx);

    cx_net_ctx_t ctx;
    ctx.c = _ctx;
    
    int32_t sock = 0;
    char* buffer = NULL;
    uint32_t* position = NULL;
    char* name = NULL;
    bool reschedule = false;

    if (CX_NET_STATE_SERVER & ctx.c->state)
    {
        CX_CHECK(cx_handle_is_valid(ctx.sv->clientsHalloc, _clientHandle),
            "[sv: %s] flushing an invalid client! (handle: %d)", ctx.c->name, _clientHandle);

        sock = ctx.sv->clients[_clientHandle].sock;
        buffer = ctx.sv->clients[_clientHandle].out;
        position = &(ctx.sv->clients[_clientHandle].outPos);
    }
    else if (CX_NET_STATE_CLIENT & ctx.c->state)
    {
        sock = ctx.cl->c.sock;
        buffer = ctx.cl->out;
        position = &(ctx.cl->outPos);
    }

    if ((*position) > 0)
    {
        int32_t bytesWritten = write(sock, buffer, *position);

        if ((*position) == bytesWritten)
        {   
            // all the bytes were written to the socket successfully
            (*position) = 0;
        }
        else if ((*position) > bytesWritten)
        {
            // there're still some bytes left in our buffer that weren't written
            // make some space by shifting our pending bytes to the left in the buffer
            int32_t bytesRemaining = (*position) - bytesWritten;
            memmove(buffer, &(buffer[bytesWritten]), bytesRemaining);
            (*position) = 0;
            reschedule = true;
        }
        else if (-1 == bytesWritten)
        {
            if (EWOULDBLOCK == errno | EAGAIN == errno)
            {
                // this write operation would block. we can't perform it now since it will
                // block our main thread. we'll re-schedule it with epoll to do it later 
                // this scenario will hardly ever happen but... 
                // the output buffer might be full eventually
                reschedule = true;
            }
            else
            {
                if (CX_NET_STATE_SERVER & ctx.c->state)
                {
                    CX_WARN(CX_ALW, "[sv: %s] [handle: %d] write failed with error: %s (errno %d)", 
                        ctx.c->name, _clientHandle, strerror(errno), errno);
                }
                else if (CX_NET_STATE_CLIENT & ctx.c->state)
                {
                    CX_WARN(CX_ALW, "[cl: %s] write failed with error: %s (errno %d)", 
                        ctx.c->name, strerror(errno), errno);
                }
            }
        }

        if (reschedule)
        {
            cx_net_epoll_mod(ctx.c->epollDescriptor, sock, true, true);
            CX_INFO("[sock: %s] write operation delayed with epoll %s (errno %d)", ctx.c->name, strerror(errno), errno); //TODO borrame
        }
    }
    // the flush operation is considered successfull if we have at least MAX_PACKET_LEN 
    // amount of bytes available in our buffer to continue appending packets
    return (CX_NET_BUFLEN - (*position)) >= MAX_PACKET_LEN;
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static bool cx_net_parse_address(const char* _ipAddress, uint16_t _port, sockaddr_in* _outAddr)
{
    CX_CHECK(strlen(_ipAddress) >= 7, "_ipAddress must have at least 7 characters");
    CX_CHECK(_port > 1023, "_port must be greater to 1023");
    CX_CHECK_NOT_NULL(_outAddr);

    memset(_outAddr, 0, sizeof(*_outAddr));
    _outAddr->sin_family = AF_INET;
    _outAddr->sin_port = htons(_port);
    int success = inet_pton(_outAddr->sin_family, _ipAddress, &(_outAddr->sin_addr));
    
    CX_WARN(success, "Invalid ip address: %s", _ipAddress);
    return success;
}

static void cx_net_poll_events_client(cx_net_ctx_cl_t* _ctx)
{
    epoll_event event;

    int32_t bytesRead = 0;

    int32_t eventsCount = epoll_wait(_ctx->c.epollDescriptor, _ctx->c.epollEvents, 1, 0);
    CX_WARN(-1 != eventsCount, "[cl: %s] epoll_wait failed - %s", _ctx->c.name, strerror(errno));

    // handle epoll event
    if (1 == eventsCount && _ctx->c.epollEvents[0].data.fd == _ctx->c.sock)
    {
        if (EPOLLIN & _ctx->c.epollEvents[0].events)
        {
            bytesRead = recv(_ctx->c.sock, &(_ctx->in[_ctx->inPos]), MAX_PACKET_LEN - _ctx->inPos, 0);
            
            if (0 == bytesRead)
            {
                CX_INFO("[cl: %s] the server closed the connection", _ctx->c.name);
                _ctx->c.state &= ~(CX_NET_STATE_CONNECTING | CX_NET_STATE_CONNECTED);
                close(_ctx->c.sock);
            }
            else if (bytesRead > 0)
            {
                cx_net_process_stream(&_ctx->c, NULL, _ctx->in, _ctx->inPos + bytesRead, &_ctx->inPos);
            }
            else
            {
                // in single-thread epoll mode recv should never block since we're 
                // the only thread dealing with this file descriptor, but anyway... 
                // it's good to know what's going on
                CX_WARN(CX_ALW, "[cl: %s] recv failed with error: %s (errno %d)", 
                    _ctx->c.name, strerror(errno), errno);
            }
        }
        else if (EPOLLOUT & _ctx->c.epollEvents[0].events)
        {
            if (CX_NET_STATE_CONNECTED & _ctx->c.state)
            {
                // pending write operation, epoll says we can do it now without blocking (let's try)
                if (cx_net_flush(_ctx, INVALID_HANDLE))
                {
                    // done. unset the EPOLLOUT flag
                    cx_net_epoll_mod(_ctx->c.epollDescriptor, _ctx->c.sock, true, false);
                }
            }
            else if (CX_NET_STATE_CONNECTING & _ctx->c.state)
            {               
                _ctx->c.state &= (~CX_NET_STATE_CONNECTING);

                if (-1 != getsockopt(_ctx->c.sock, SOL_SOCKET, SO_ERROR, &(_ctx->c.errorNumber), &(socklen_t){ sizeof(_ctx->c.errorNumber) }))
                {
                    if (_ctx->c.errorNumber == 0)
                    {
                        _ctx->c.state |= CX_NET_STATE_CONNECTED;
                        cx_net_epoll_mod(_ctx->c.epollDescriptor, _ctx->c.sock, true, false);

                        CX_INFO("[cl: %s] connection established with server on %s:%d", 
                            _ctx->c.name, _ctx->c.ip, _ctx->c.port);
                    }
                }
                else
                {
                    _ctx->c.errorNumber = errno;
                }

                if (0 != _ctx->c.errorNumber)
                {
                    _ctx->c.state |= CX_NET_STATE_ERROR;
                    CX_WARN(CX_ALW, "[cl: %s] connection with server on %s:%d failed - %s (errno %d)",
                        _ctx->c.name, _ctx->c.ip, _ctx->c.port, strerror(_ctx->c.errorNumber), _ctx->c.errorNumber);
                }

            }
        }
    }

    //TODO fixme, do this only every 16ms or so
    if (_ctx->outPos > 0)
    {
        cx_net_flush(_ctx, INVALID_HANDLE);
    }
}

static void cx_net_poll_events_server(cx_net_ctx_sv_t* _ctx)
{
    epoll_event event;
    sockaddr_in address;

    int32_t bytesRead = 0;
    int32_t clientSock = 0;
    uint16_t clientHandle = INVALID_HANDLE;
    cx_net_client_t* client = NULL;

    int32_t eventsCount = epoll_wait(_ctx->c.epollDescriptor,
        _ctx->c.epollEvents, _ctx->clientsMax, 0);

    CX_WARN(-1 != eventsCount, "[sv: %s] epoll_wait failed", _ctx->c.name);

    // handle epoll events
    for (uint32_t i = 0; i < eventsCount; i++)
    {
        if (_ctx->c.epollEvents[i].data.fd == _ctx->c.sock)
        {
            // incoming connection from our listening socket
            CX_MEM_ZERO(address);
            CX_MEM_ZERO(event);

            clientSock = accept(_ctx->c.sock, &(address), &(socklen_t) { sizeof(address) });
            if (INVALID_DESCRIPTOR != clientSock)
            {
                clientHandle = cx_handle_alloc_key(_ctx->clientsHalloc, clientSock);
                if (INVALID_HANDLE != clientHandle)
                {
                    event.events = EPOLLIN;
                    event.data.fd = clientSock;
                    epoll_ctl(_ctx->c.epollDescriptor, EPOLL_CTL_ADD, clientSock, &event);

                    _ctx->clients[clientHandle].handle = clientHandle;
                    _ctx->clients[clientHandle].sock = clientSock;
                    _ctx->clients[clientHandle].inPos = 0;
                    _ctx->clients[clientHandle].outPos = 0;
                    inet_ntop(AF_INET, &(address.sin_addr), _ctx->clients[clientHandle].ip, INET_ADDRSTRLEN);

                    CX_INFO("[sv: %s] [handle: %d] client connected (ip: %s, socket: %d)", _ctx->c.name, clientHandle, _ctx->clients[clientHandle].ip, clientSock);
                }
                else
                {
                    CX_WARN(CX_ALW, "[sv: %s] the clients container is full. a new incoming connection was rejected. (socket %d)", _ctx->c.name, clientSock);
                    close(clientSock);
                }
            }
            else
            {
                // in single-thread epoll mode accept should never block since we're the only 
                // thread dealing with this file descriptor and in the worst case
                // accept will just report that there's nothing to accept... but anyway
                CX_WARN(CX_ALW, "[sv: %s] accept failed with error: %s (errno %d)", _ctx->c.name, strerror(errno), errno);
            }
        }
        else
        {
            // incoming data from an active connection
            // we need to figure out from which client this is coming from to handle it properly
            clientSock = _ctx->c.epollEvents[i].data.fd;
            clientHandle = cx_handle_get(_ctx->clientsHalloc, clientSock);
            if (INVALID_HANDLE != clientHandle)
            {
                client = &(_ctx->clients[clientHandle]);

                if (EPOLLIN & _ctx->c.epollEvents[i].events)
                {
                    bytesRead = recv(client->sock, &(client->in[client->inPos]), CX_NET_BUFLEN - client->inPos, 0);

                    if (0 == bytesRead)
                    {
                        CX_INFO("[sv: %s] [handle: %d] client disconnected (ip: %s, socket: %d)",
                            _ctx->c.name, clientHandle, _ctx->clients[clientHandle].ip, clientSock);

                        close(client->sock);
                        cx_handle_free(_ctx->clientsHalloc, clientHandle);
                    }
                    else if (bytesRead > 0)
                    {
                        cx_net_process_stream(&_ctx->c, client,
                            client->in, client->inPos + bytesRead, &client->inPos);
                    }
                    else
                    {
                        // in single-thread epoll mode recv should never block since we're 
                        // the only thread dealing with this file descriptor, but anyway... 
                        // it's good to know what's going on
                        CX_WARN(CX_ALW, "[sv: %s] [handle: %d] recv failed with error: %s (errno %d)", _ctx->c.name, clientHandle, strerror(errno), errno);
                    }
                }

                if (EPOLLOUT & _ctx->c.epollEvents[i].events)
                {
                    // pending write operation, epoll says we can do it now without blocking (let's see)
                    if (cx_net_flush(_ctx, clientHandle))
                    {
                        // done. get rid of the EPOLLOUT flag
                        CX_MEM_ZERO(event);
                        event.events = EPOLLIN;
                        event.data.fd = client->sock;
                        epoll_ctl(_ctx->c.epollDescriptor, EPOLL_CTL_MOD, client->sock, &event);
                    }
                }
            }
            CX_WARN(INVALID_HANDLE != clientHandle, "[sv: %s] socket %d is not registered as a client!", _ctx->c.name, clientSock);
        }
    }

    //TODO fixme, do this only every 16ms or so
    //TODO ping-pong/keep alive/shut-down checks here
    uint16_t clientsCount = cx_handle_count(_ctx->clientsHalloc);
    uint16_t handle = INVALID_HANDLE;
    for (uint16_t i = 0; i < clientsCount; i++)
    {
        handle = cx_handle_at(_ctx->clientsHalloc, i);
        if (_ctx->clients[handle].outPos > 0)
        {
            cx_net_flush(_ctx, handle);
        }
    }
}
static void cx_net_process_stream(const cx_net_common_t* _common, void* _passThru, 
    char* _buffer, uint32_t _bufferSize, uint32_t* _outPos)
{   
    uint32_t bytesParsed = 0;       // current amount of bytes parsed from the given buffer
    uint32_t bytesRemaining = 0;    // remaining bytes that can't be parsed at this time (incomplete buffer)
    uint8_t packetHeader = 0;       // 8-bit unsigned integer packet identifier
    uint16_t packetLength = 0;      // length of the "body" of the packet (does not include the header/length bytes)
    cx_net_handler_cb packetHandler = NULL;

    while ((_bufferSize - bytesParsed) >= MIN_PACKET_LEN)
    {
        cx_binr_uint8(_buffer, &bytesParsed, &packetHeader);
        cx_binr_uint16(_buffer, &bytesParsed, &packetLength);
        
        if ((_bufferSize - bytesParsed) >= packetLength)
        {
            // we have enough bytes to process this packet
            packetHandler = _common->msgHandlers[packetHeader];
            if (NULL != packetHandler)
            {
                packetHandler(_common, _passThru, &(_buffer[bytesParsed]));
            }
            CX_WARN(NULL != packetHandler, "[sv: %s] message handler for packet #%d is not defined", _common->name, packetHeader);

            bytesParsed += packetLength;
        }
        else
        {
            // the packet is not complete yet, we need to wait for some more bytes to 
            // arrive before we can fully process it
            bytesParsed -= 3;
            break;
        }
    }

    if (bytesParsed < _bufferSize)
    {
        // there're still some remaining bytes in our buffer that need to be parsed in
        // the next call. shift our data to the beginning of the buffer to make some new space
        bytesRemaining = _bufferSize - bytesParsed;
        memmove(_buffer, &(_buffer[bytesParsed]), bytesRemaining);
        (*_outPos) = bytesRemaining;
    }
    else
    {
        (*_outPos) = 0;
    }
}

static void cx_net_epoll_mod(int32_t _epollDescriptor, int32_t _sock, bool _in, bool _out)
{
    epoll_event event;
    CX_MEM_ZERO(event);

    event.data.fd = _sock;
    event.events = 0
        | (_in ? EPOLLIN : 0)
        | (_out ? EPOLLOUT : 0);

    epoll_ctl(_epollDescriptor, EPOLL_CTL_MOD, _sock, &event);
}
