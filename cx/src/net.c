#include "cx.h"
#include "net.h"
#include "mem.h"
#include "str.h"
#include "binr.h"
#include "binw.h"
#include "timer.h"
#include "math.h"

#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h> 
#include <errno.h>
#include <sys/epoll.h>
#include <string.h>
#include <pthread.h>

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool             _cx_net_parse_address(const char* _ipAddress, uint16_t _port, sockaddr_in* _outAddr);

static void             _cx_net_poll_events_client(cx_net_ctx_cl_t* _ctx, int32_t _timeout);

static void             _cx_net_poll_events_server(cx_net_ctx_sv_t* _ctx, int32_t _timeout);

static bool             _cx_net_process_stream(cx_net_common_t* _common, void* _userData, char* _buffer, uint32_t _bufferSize, uint32_t* _inOutPos);

static void             _cx_net_epoll_mod(int32_t _epollDescriptor, int32_t _sock, bool _in, bool _out);

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
    ctx->c.validationTimeout = cx_math_max(5, _args->validationTimeout);
    ctx->clientsMax = _args->maxClients > 0 ? _args->maxClients : 1;
    ctx->onConnection = _args->onConnection;
    ctx->onDisconnection = _args->onDisconnection;
    ctx->userData = _args->userData;

    if (_args->multiThreadedSend)
    {
        CX_INFO("[-->%s] initializing mutexes...", ctx->c.name);
        ctx->c.mtxInitialized = (0 == pthread_mutex_init(&ctx->c.mtx, NULL));
    }

    if (!_args->multiThreadedSend || ctx->c.mtxInitialized)
    {
        sockaddr_in address;
        if (_cx_net_parse_address(ctx->c.ip, ctx->c.port, &address))
        {
            CX_INFO("[%s<--] creating socket...", ctx->c.name);
            ctx->c.sock = socket(AF_INET, SOCK_STREAM, 0);
            if (INVALID_DESCRIPTOR != ctx->c.sock)
            {
                CX_INFO("[%s<--] setting socket options...", ctx->c.name);
                if (-1 != setsockopt(ctx->c.sock, SOL_SOCKET, SO_REUSEADDR, &(int32_t){ 1 }, sizeof(int32_t))
                    && -1 != fcntl(ctx->c.sock, F_SETFL, fcntl(ctx->c.sock, F_GETFL, 0) | O_NONBLOCK))
                {
                    CX_INFO("[%s<--] binding on port %d...", ctx->c.name, ctx->c.port);
                    if (-1 != bind(ctx->c.sock, (struct sockaddr*)&address, sizeof(address)))
                    {
                        CX_INFO("[%s<--] listening on %s:%d...", ctx->c.name, ctx->c.ip, ctx->c.port);
                        if (-1 != listen(ctx->c.sock, SOMAXCONN))
                        {
                            CX_INFO("[%s<--] initializing event pooling...", ctx->c.name);
                            ctx->c.epollDescriptor = epoll_create(ctx->clientsMax);
                            if (INVALID_DESCRIPTOR != ctx->c.epollDescriptor)
                            {
                                ctx->c.epollEvents = CX_MEM_ARR_ALLOC(ctx->c.epollEvents, ctx->clientsMax);
                                ctx->clients = CX_MEM_ARR_ALLOC(ctx->clients, ctx->clientsMax);
                                ctx->clientsHalloc = cx_halloc_init(ctx->clientsMax);
                                ctx->tmpHandles = CX_MEM_ARR_ALLOC(ctx->tmpHandles, ctx->clientsMax);

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
                                CX_INFO("[%s<--] started serving on %s:%d", ctx->c.name, ctx->c.ip, ctx->c.port);
                            }
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
        CX_WARN(CX_ALW, "[%s<--] listen failed: %s (%s:%d)", ctx->c.name, strerror(errno), ctx->c.ip, ctx->c.port);

        cx_net_destroy(ctx);
        ctx = NULL;
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
    ctx->c.validationTimeout = cx_math_max(5, _args->validationTimeout);
    ctx->onConnected = _args->onConnected;
    ctx->onDisconnected = _args->onDisconnected;
    ctx->userData = _args->userData;

    epoll_event event;
    CX_MEM_ZERO(event);

    if (_args->multiThreadedSend)
    {
        CX_INFO("[-->%s] initializing mutexes...", ctx->c.name);
        ctx->c.mtxInitialized = (0 == pthread_mutex_init(&ctx->c.mtx, NULL));
    }

    if (!_args->multiThreadedSend || ctx->c.mtxInitialized)
    {
        sockaddr_in address;
        if (_cx_net_parse_address(ctx->c.ip, ctx->c.port, &address))
        {
            CX_INFO("[-->%s] creating socket...", ctx->c.name);
            ctx->c.sock = socket(AF_INET, SOCK_STREAM, 0);
            if (-1 != ctx->c.sock)
            {
                CX_INFO("[-->%s] setting socket options...", ctx->c.name);
                if (-1 != fcntl(ctx->c.sock, F_SETFL, fcntl(ctx->c.sock, F_GETFL, 0) | O_NONBLOCK))
                {
                    CX_INFO("[-->%s] initializing event pooling...", ctx->c.name);
                    ctx->c.epollDescriptor = epoll_create(1);
                    if (INVALID_DESCRIPTOR != ctx->c.epollDescriptor)
                    {
                        event.events = EPOLLIN;
                        event.data.fd = ctx->c.sock;

                        CX_INFO("[-->%s] connecting to %s:%d...", ctx->c.name, ctx->c.ip, ctx->c.port);
                        if (-1 != connect(ctx->c.sock, (struct sockaddr*)&address, sizeof(address)))
                        {
                            ctx->c.state &= ~CX_NET_STATE_ERROR;
                            ctx->c.state |= CX_NET_STATE_CONNECTED;
                            CX_INFO("[-->%s] connection established to server on %s:%d", ctx->c.name, ctx->c.ip, ctx->c.port);
                        }
                        else if (EINPROGRESS == errno || EAGAIN == errno)
                        {
                            // operation can't be performed right now, but epoll will let us know when it's done
                            event.events |= EPOLLOUT;
                            ctx->c.state &= ~CX_NET_STATE_ERROR;
                            ctx->c.state |= CX_NET_STATE_CONNECTING;
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

        ctx->c.errorNumber = errno;
        CX_WARN(CX_ALW, "[-->%s] connect failed: %s (%s:%d)", ctx->c.name, strerror(errno), ctx->c.ip, ctx->c.port);

        cx_net_destroy(ctx);
        ctx = NULL;
    }
    else
    {
        // register our desired events in the epoll instance
        epoll_ctl(ctx->c.epollDescriptor, EPOLL_CTL_ADD, ctx->c.sock, &event);
        ctx->c.epollEvents = CX_MEM_ARR_ALLOC(ctx->c.epollEvents, 1);

        if (_args->connectBlocking)
        {
            _cx_net_poll_events_client(ctx, _args->connectTimeout);
        }
    }

    return ctx;
}

void cx_net_destroy(void* _ctx)
{
    if (NULL == _ctx) return;

    cx_net_ctx_t ctx = { _ctx };
    ctx.c->state |= CX_NET_STATE_CLOSING;

    if (CX_NET_STATE_SERVER & ctx.c->state)
    {
        // close the listening socket
        if (INVALID_DESCRIPTOR != ctx.c->sock)
        {
            close(ctx.c->sock);
            epoll_ctl(ctx.c->epollDescriptor, EPOLL_CTL_DEL, ctx.c->sock, NULL);
            ctx.c->sock = INVALID_DESCRIPTOR;
        }

        //flush & disconnect all clients
        if (NULL != ctx.sv->clientsHalloc)
        {
            uint16_t clientsCount = cx_handle_count(ctx.sv->clientsHalloc);
            uint16_t handle = INVALID_HANDLE;
            uint16_t j = 0;
            for (uint16_t i = 0; i < clientsCount; i++)
            {
                handle = cx_handle_at(ctx.sv->clientsHalloc, i);
                ctx.sv->tmpHandles[j++] = handle;
                cx_net_flush(ctx.sv, handle);
            }

            _cx_net_poll_events_server(ctx.sv, 0);

            for (uint16_t i = 0; i < j; i++)
            {
                cx_net_disconnect(ctx.sv, ctx.sv->tmpHandles[i], "context closed");
            }

            cx_halloc_destroy(ctx.sv->clientsHalloc);
            ctx.sv->clientsHalloc = NULL;

            free(ctx.sv->clients);
            ctx.sv->clients = NULL;
        }
        
        if (NULL != ctx.sv->tmpHandles)
        {
            free(ctx.sv->tmpHandles);
            ctx.sv->tmpHandles = NULL;
        }
        
        CX_INFO("[%s<--] finished serving on %s:%d", ctx.c->name, ctx.c->ip, ctx.c->port);
    }
    else if (CX_NET_STATE_CLIENT & ctx.c->state)
    {
        if (CX_NET_STATE_CONNECTED & ctx.c->state)
        {
            cx_net_flush(ctx.cl, INVALID_HANDLE);

            _cx_net_poll_events_client(ctx.cl, 0);

            cx_net_disconnect(ctx.cl, INVALID_HANDLE, "context closed");
        }
    }

    // destroy epoll instance and free epoll events array
    if (INVALID_DESCRIPTOR != ctx.c->epollDescriptor)
    {
        close(ctx.c->epollDescriptor);
        ctx.c->epollDescriptor = INVALID_DESCRIPTOR;
    }

    if (NULL != ctx.c->epollEvents)
    {
        free(ctx.c->epollEvents);
        ctx.c->epollEvents = NULL;
    }
    
    // destroy mutexes
    if (ctx.c->mtxInitialized)
    {
        pthread_mutex_destroy(&ctx.c->mtx);
        ctx.c->mtxInitialized = false;
    }

    free(_ctx);
}

void cx_net_poll_events(void* _ctx, int32_t _timeout)
{
    CX_CHECK_NOT_NULL(_ctx);

    cx_net_ctx_t ctx;
    ctx.c = _ctx;

    if (CX_NET_STATE_SERVER & ctx.c->state)
    {
        if (CX_NET_STATE_LISTENING & ctx.c->state)
        {
            _cx_net_poll_events_server(ctx.sv, _timeout);
        }
    }
    else if (CX_NET_STATE_CLIENT & ctx.c->state)
    {
        if ((CX_NET_STATE_CONNECTING | CX_NET_STATE_CONNECTED) & ctx.c->state)
        {
            _cx_net_poll_events_client(ctx.cl, _timeout);
        }
    }
}

int32_t cx_net_send(void* _ctx, uint8_t _header, const char* _payload, uint32_t _payloadSize, uint16_t _clientHandle)
{
    int32_t result = CX_NET_SEND_OK;

    CX_CHECK_NOT_NULL(_ctx);
    cx_net_ctx_t ctx = { _ctx };

    CX_CHECK(_payloadSize <= MAX_PACKET_LEN - MIN_PACKET_LEN, "_payloadSize can't be greater than %d bytes", MAX_PACKET_LEN - MIN_PACKET_LEN);
    if (_payloadSize > MAX_PACKET_LEN - MIN_PACKET_LEN) return false;

    if (ctx.c->mtxInitialized) pthread_mutex_lock(&ctx.c->mtx);

    uint32_t bytesRequired = MIN_PACKET_LEN + _payloadSize;
    char* buffer = NULL;
    uint16_t bufferSize = 0;
    uint32_t* position = NULL;

    if (CX_NET_STATE_SERVER & ctx.c->state)
    {
        //TODO check the client is still valid (versioning?) and also connceted to us
        CX_CHECK(cx_handle_is_valid(ctx.sv->clientsHalloc, _clientHandle),
            "[%s<--] sending a packet to an invalid client! (handle: %d)", ctx.c->name, _clientHandle);

        buffer = ctx.sv->clients[_clientHandle].out;
        bufferSize = sizeof(ctx.sv->clients[_clientHandle].out);
        position = &(ctx.sv->clients[_clientHandle].outPos);
    }
    else if (CX_NET_STATE_CLIENT & ctx.c->state)
    {
        if (CX_NET_STATE_CONNECTED & ctx.c->state)
        {
            buffer = ctx.cl->out;
            bufferSize = sizeof(ctx.cl->out);
            position = &(ctx.cl->outPos);
        }
        else
        {
            result = CX_NET_SEND_DISCONNECTED;
            goto send_finished;
        }
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
                CX_WARN(CX_ALW, "[%s<--] [handle: %d] we ran out of outbound buffer space to write packet #%d of length %d bytes",
                    ctx.c->name, _clientHandle, _header, bytesRequired);
            }
            else if (CX_NET_STATE_CLIENT & ctx.c->state)
            {
                CX_WARN(CX_ALW, "[-->%s] we ran out of outbound buffer space to write packet #%d of length %d bytes",
                    ctx.c->name, _header, bytesRequired);
            }

            result = CX_NET_SEND_BUFFER_FULL;
            goto send_finished;
        }
    }

    // write the packet to our buffer
    cx_binw_uint8(buffer, bufferSize - (*position), position, _header);
    cx_binw_uint16(buffer, bufferSize - (*position), position, _payloadSize);

    if (_payloadSize > 0)
    {
        CX_CHECK_NOT_NULL(_payload);
        memcpy(&(buffer[*position]), _payload, _payloadSize);
        (*position) += _payloadSize;
    }

send_finished:

    if (ctx.c->mtxInitialized) pthread_mutex_unlock(&ctx.c->mtx);

    return result;
}

void cx_net_validate(void* _ctx, uint16_t _clientHandle)
{
    CX_CHECK_NOT_NULL(_ctx);
    cx_net_ctx_t ctx = { _ctx };

    if (CX_NET_STATE_SERVER & ctx.c->state)
    {
        CX_CHECK(INVALID_HANDLE != _clientHandle, "_clientHandle is invalid!");
        cx_net_client_t* client = &ctx.sv->clients[_clientHandle];
        CX_CHECK(!client->validated, "client connection is already validated!");
        client->validated = true;
    }
    else if (CX_NET_STATE_CLIENT & ctx.c->state)
    {
        CX_CHECK(!ctx.cl->validated, "server connection is already validated!");
        ctx.cl->validated = true;
    }
}

bool cx_net_flush(void* _ctx, uint16_t _clientHandle)
{
    CX_CHECK_NOT_NULL(_ctx);
    cx_net_ctx_t ctx = { _ctx };

    int32_t sock = 0;
    char* buffer = NULL;
    uint32_t* position = NULL;
    bool reschedule = false;

    if (CX_NET_STATE_SERVER & ctx.c->state)
    {
        CX_CHECK(cx_handle_is_valid(ctx.sv->clientsHalloc, _clientHandle),
            "[%s<--] flushing an invalid client! (handle: %d)", ctx.c->name, _clientHandle);

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
            if (EWOULDBLOCK == errno || EAGAIN == errno)
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
                    CX_WARN(CX_ALW, "[%s<--] [handle: %d] write failed with error: %s (errno %d)", 
                        ctx.c->name, _clientHandle, strerror(errno), errno);
                }
                else if (CX_NET_STATE_CLIENT & ctx.c->state)
                {
                    CX_WARN(CX_ALW, "[-->%s] write failed with error: %s (errno %d)", 
                        ctx.c->name, strerror(errno), errno);
                }
            }
        }

        if (reschedule)
        {
            _cx_net_epoll_mod(ctx.c->epollDescriptor, sock, true, true);
            CX_INFO("[sock: %s] write operation delayed with epoll %s (errno %d)", ctx.c->name, strerror(errno), errno); //TODO borrame
        }
    }
    // the flush operation is considered successfull if we have at least MAX_PACKET_LEN 
    // amount of bytes available in our buffer to continue appending packets
    return (CX_NET_BUFLEN - (*position)) >= MAX_PACKET_LEN;
}

void cx_net_disconnect(void* _ctx, uint16_t _clientHandle, const char* _reason)
{
    CX_CHECK_NOT_NULL(_ctx);
    cx_net_ctx_t ctx = { _ctx };

    if (CX_NET_STATE_SERVER & ctx.c->state)
    {
        CX_CHECK(INVALID_HANDLE != _clientHandle, "_clientHandle is invalid!");
        cx_net_client_t* client = &ctx.sv->clients[_clientHandle];

        CX_INFO("[%s<--] [handle: %d] client disconnected (ip: %s, socket: %d, reason: %s)",
            ctx.c->name, _clientHandle, client->ip, client->sock, _reason != NULL ? _reason : "unknown");

        if (INVALID_DESCRIPTOR != client->sock)
        {
            epoll_ctl(ctx.c->epollDescriptor, EPOLL_CTL_DEL, client->sock, NULL);
            close(client->sock);
            client->sock = INVALID_DESCRIPTOR;
        }

        if (NULL != ctx.sv->onDisconnection) 
            ctx.sv->onDisconnection(ctx.sv, &ctx.sv->clients[_clientHandle]);

        cx_handle_free(ctx.sv->clientsHalloc, _clientHandle);
    }
    else if (CX_NET_STATE_CLIENT & ctx.c->state)
    {
        CX_INFO("[-->%s] server disconnected (ip: %s, socket: %d, reason: %s)", 
            ctx.c->name, ctx.c->ip, ctx.c->sock, _reason);

        if (ctx.c->mtxInitialized) pthread_mutex_lock(&ctx.c->mtx);
        ctx.c->state &= ~(CX_NET_STATE_CONNECTING | CX_NET_STATE_CONNECTED);
        if (ctx.c->mtxInitialized) pthread_mutex_unlock(&ctx.c->mtx);

        if (INVALID_DESCRIPTOR != ctx.c->sock)
        {
            epoll_ctl(ctx.c->epollDescriptor, EPOLL_CTL_DEL, ctx.c->sock, NULL);
            close(ctx.c->sock);
            ctx.c->sock = INVALID_DESCRIPTOR;
        }

        if (NULL != ctx.cl->onDisconnected) 
            ctx.cl->onDisconnected(ctx.cl);
    }
}

void cx_net_wait_outboundbuff(void* _ctx, uint16_t _clientHandle, int32_t _timeout)
{
    //TODO
    CX_WARN(CX_ALW, "cx_net_wait_outboundbuff is not implemented yet!");
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static bool _cx_net_parse_address(const char* _ipAddress, uint16_t _port, sockaddr_in* _outAddr)
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

static void _cx_net_poll_events_client(cx_net_ctx_cl_t* _ctx, int32_t _timeout)
{
    double time = cx_time_counter();
    int32_t bytesRead = 0;

    int32_t eventsCount = epoll_wait(_ctx->c.epollDescriptor, _ctx->c.epollEvents, 1, _timeout);
    CX_WARN(-1 != eventsCount, "[-->%s] epoll_wait failed - %s", _ctx->c.name, strerror(errno));

    // handle epoll event
    if (1 == eventsCount && _ctx->c.epollEvents[0].data.fd == _ctx->c.sock)
    {
        if (EPOLLIN & _ctx->c.epollEvents[0].events)
        {
            errno = 0;
            bytesRead = recv(_ctx->c.sock, &(_ctx->in[_ctx->inPos]), MAX_PACKET_LEN - _ctx->inPos, 0);
            
            if (0 == bytesRead)
            {
                cx_net_disconnect(_ctx, INVALID_HANDLE, "server closed the connection");
            }
            else if (bytesRead > 0)
            {
                _ctx->lastPacketTime = time;
                _cx_net_process_stream(&_ctx->c, NULL, _ctx->in, _ctx->inPos + bytesRead, &_ctx->inPos);
            }
            else if (-1 == bytesRead && errno == ECONNREFUSED)
            {
                cx_net_disconnect(_ctx, INVALID_HANDLE, "server refused the connection");
            }
            else
            {
                // in single-thread epoll mode recv should never block since we're 
                // the only thread dealing with this file descriptor, but anyway... 
                // it's good to know what's going on
                CX_WARN(CX_ALW, "[-->%s] recv failed with error: %s (errno %d)", 
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
                    _cx_net_epoll_mod(_ctx->c.epollDescriptor, _ctx->c.sock, true, false);
                }
            }
            else if (CX_NET_STATE_CONNECTING & _ctx->c.state)
            {               
                _ctx->c.state &= ~CX_NET_STATE_CONNECTING;

                if (-1 != getsockopt(_ctx->c.sock, SOL_SOCKET, SO_ERROR, &(_ctx->c.errorNumber), &(socklen_t){ sizeof(_ctx->c.errorNumber) }))
                {
                    if (_ctx->c.errorNumber == 0)
                    {
                        _ctx->validated = false;
                        _ctx->connectedTime = time;
                        _ctx->lastPacketTime = time;

                        _ctx->c.state |= CX_NET_STATE_CONNECTED;
                        _cx_net_epoll_mod(_ctx->c.epollDescriptor, _ctx->c.sock, true, false);
                        
                        CX_INFO("[-->%s] connection established with server on %s:%d", 
                            _ctx->c.name, _ctx->c.ip, _ctx->c.port);

                        if (NULL != _ctx->onConnected)
                            _ctx->onConnected(_ctx);
                    }
                }
                else
                {
                    _ctx->c.errorNumber = errno;
                }

                if (0 != _ctx->c.errorNumber)
                {
                    _ctx->c.state |= CX_NET_STATE_ERROR;
                    CX_WARN(CX_ALW, "[-->%s] connection with server on %s:%d failed - %s (errno %d)",
                        _ctx->c.name, _ctx->c.ip, _ctx->c.port, strerror(_ctx->c.errorNumber), _ctx->c.errorNumber);
                }
            }
        }
    }

    if ((CX_NET_STATE_CONNECTED & _ctx->c.state))
    {
        if (time - _ctx->lastPacketTime > (CX_NET_INACTIVITY_TIMEOUT * 0.5))
        {
            _ctx->lastPacketTime = time;
            cx_net_send(_ctx, CX_NETP_PING, NULL, 0, INVALID_HANDLE);
        }

        //TODO fixme, do this only every 16ms or so
        if (_ctx->outPos > 0)
        {
            cx_net_flush(_ctx, INVALID_HANDLE);
        }
    }
}

static void _cx_net_poll_events_server(cx_net_ctx_sv_t* _ctx, int32_t _timeout)
{
    double time = cx_time_counter();

    epoll_event event;
    sockaddr_in address;
    ipv4_t ipv4;

    int32_t bytesRead = 0;
    int32_t clientSock = 0;
    uint16_t clientHandle = INVALID_HANDLE;
    cx_net_client_t* client = NULL;

    int32_t eventsCount = epoll_wait(_ctx->c.epollDescriptor,
        _ctx->c.epollEvents, _ctx->clientsMax, _timeout);

    CX_WARN(-1 != eventsCount, "[%s<--] epoll_wait failed", _ctx->c.name);

    // handle epoll events
    for (int32_t i = 0; i < eventsCount; i++)
    {
        if (_ctx->c.epollEvents[i].data.fd == _ctx->c.sock)
        {
            // incoming connection from our listening socket
            CX_MEM_ZERO(address);
            CX_MEM_ZERO(event);

            clientSock = accept(_ctx->c.sock, (struct sockaddr * restrict)&address, &(socklen_t) { sizeof(address) });
            if (INVALID_DESCRIPTOR != clientSock)
            {
                inet_ntop(AF_INET, &(address.sin_addr), ipv4, INET_ADDRSTRLEN);

                if (!(CX_NET_STATE_CLOSING & _ctx->c.state) &&
                    (NULL == _ctx->onConnection || _ctx->onConnection(_ctx, ipv4)))
                {
                    clientHandle = cx_handle_alloc_key(_ctx->clientsHalloc, clientSock);
                    if (INVALID_HANDLE != clientHandle)
                    {
                        event.events = EPOLLIN;
                        event.data.fd = clientSock;
                        epoll_ctl(_ctx->c.epollDescriptor, EPOLL_CTL_ADD, clientSock, &event);

                        _ctx->clients[clientHandle].handle = clientHandle;
                        _ctx->clients[clientHandle].validated = false;
                        _ctx->clients[clientHandle].connectedTime = time;
                        _ctx->clients[clientHandle].lastPacketTime = time;
                        _ctx->clients[clientHandle].sock = clientSock;
                        _ctx->clients[clientHandle].inPos = 0;
                        _ctx->clients[clientHandle].outPos = 0;
                        cx_str_copy(_ctx->clients[clientHandle].ip, sizeof(ipv4), ipv4);

                        CX_INFO("[%s<--] [handle: %d] client connected (ip: %s, socket: %d)", _ctx->c.name, clientHandle, _ctx->clients[clientHandle].ip, clientSock);
                    }
                    else
                    {
                        CX_WARN(CX_ALW, "[%s<--] the clients container is full. a new incoming connection was rejected. (socket %d)", _ctx->c.name, clientSock);
                        close(clientSock);
                    }
                }
                else
                {
                    // connection rejected.
                    close(clientSock);
                }
            }
            else
            {
                // in single-thread epoll mode accept should never block since we're the only 
                // thread dealing with this file descriptor and in the worst case
                // accept will just report that there's nothing to accept... but anyway
                CX_WARN(CX_ALW, "[%s<--] accept failed with error: %s (errno %d)", _ctx->c.name, strerror(errno), errno);
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
                    errno = 0;
                    bytesRead = recv(client->sock, &(client->in[client->inPos]), CX_NET_BUFLEN - client->inPos, 0);

                    if (0 == bytesRead)
                    {
                        cx_net_disconnect(_ctx, clientHandle, "client closed the connection");
                    }
                    else if (bytesRead > 0)
                    {
                        client->lastPacketTime = time;
                        if (!_cx_net_process_stream(&_ctx->c, client, client->in, client->inPos + bytesRead, &client->inPos))
                        {
                            cx_net_disconnect(_ctx, clientHandle, "invalid packet");
                        }
                    }
                    else
                    {
                        // in single-thread epoll mode recv should never block since we're 
                        // the only thread dealing with this file descriptor, but anyway... 
                        // it's good to know what's going on
                        CX_WARN(CX_ALW, "[%s<--] [handle: %d] recv failed with error: %s (errno %d)", _ctx->c.name, clientHandle, strerror(errno), errno);
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
            CX_WARN(INVALID_HANDLE != clientHandle, "[%s<--] socket %d is not registered as a client!", _ctx->c.name, clientSock);
        }
    }

    //TODO fixme, do this only every 16ms or so
    uint16_t clientsCount = cx_handle_count(_ctx->clientsHalloc);
    uint16_t handle = INVALID_HANDLE;
    uint16_t validationCount = 0;
    uint16_t inactiveCount = 0;
    for (uint16_t i = 0; i < clientsCount; i++)
    {
        handle = cx_handle_at(_ctx->clientsHalloc, i);
        client = &_ctx->clients[handle];

        if (client->outPos > 0)
            cx_net_flush(_ctx, handle);

        if (!client->validated && time - client->connectedTime > _ctx->c.validationTimeout)
            _ctx->tmpHandles[validationCount++] = handle;

        if (time - client->lastPacketTime > CX_NET_INACTIVITY_TIMEOUT)
            _ctx->tmpHandles[_ctx->clientsMax - 1 - (inactiveCount++)] = handle;
    }

    for (uint16_t i = 0; i < validationCount; i++)
    {
        cx_net_disconnect(_ctx, _ctx->tmpHandles[i], "validation handshake timed-out");
    }

    for (uint16_t i = 0; i < inactiveCount; i++)
    {
        cx_net_disconnect(_ctx, _ctx->tmpHandles[_ctx->clientsMax - 1 - i], "inactive");
    }
}

static bool _cx_net_process_stream(cx_net_common_t* _common, void* _userData,
    char* _buffer, uint32_t _bufferSize, uint32_t* _outPos)
{   
    const cx_net_ctx_t ctx = { _common };
    bool     success = true;
    uint32_t bytesParsed = 0;       // current amount of bytes parsed from the given buffer
    uint32_t bytesRemaining = 0;    // remaining bytes that can't be parsed at this time (incomplete buffer)
    uint8_t  packetHeader = 0;      // 8-bit unsigned integer packet identifier
    uint16_t packetLength = 0;      // length of the "body" of the packet (does not include the header/length bytes)
    bool     packetValid = false;   // true if the packet is valid, and can be handled.
    cx_net_handler_cb packetHandler = NULL;

    while ((_bufferSize - bytesParsed) >= MIN_PACKET_LEN)
    {
        cx_binr_uint8(_buffer, _bufferSize, &bytesParsed, &packetHeader);
        cx_binr_uint16(_buffer, _bufferSize, &bytesParsed, &packetLength);
        
        if ((_bufferSize - bytesParsed) >= packetLength)
        {
            // we have enough bytes to process this 
            packetHandler = packetHeader > _CX_NETP_BEGIN_
                ? _common->msgHandlers[packetHeader]
                : NULL;

            packetValid = false
                || ((CX_NET_STATE_SERVER & ctx.c->state) && (false
                    || ((cx_net_client_t*)_userData)->validated 
                    || packetHeader == CX_NETP_AUTH))
                || ((CX_NET_STATE_CLIENT & ctx.c->state) && (false
                    || ctx.cl->validated
                    || packetHeader == CX_NETP_ACK));
        
            if (packetValid && NULL != packetHandler)
            {
                packetHandler(_common, _userData, &(_buffer[bytesParsed]), packetLength);
            }
            else if (CX_NETP_PING == packetHeader)
            {
                cx_net_send((void*)ctx.c, CX_NETP_PONG, NULL, 0, CX_NET_STATE_SERVER & ctx.c->state 
                    ? ((cx_net_client_t*)_userData)->handle
                    : INVALID_HANDLE);
            }
            else if (CX_NETP_PONG == packetHeader)
            {
                //noop.
            }
            else
            {
                success = false;
                CX_WARN(NULL != packetHandler, "[%s<--] message handler for packet #%d is not defined", _common->name, packetHeader);
            }

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

    return success;
}

static void _cx_net_epoll_mod(int32_t _epollDescriptor, int32_t _sock, bool _in, bool _out)
{
    epoll_event event;
    CX_MEM_ZERO(event);

    event.data.fd = _sock;
    event.events = 0
        | (_in ? EPOLLIN : 0)
        | (_out ? EPOLLOUT : 0);

    epoll_ctl(_epollDescriptor, EPOLL_CTL_MOD, _sock, &event);
}
