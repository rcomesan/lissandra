#ifndef CX_NET_H_
#define CX_NET_H_

#include "halloc.h"

#include <sys/socket.h>
#include <stdbool.h>

#define MAX_PACKET_LEN 4096
#define MIN_PACKET_LEN 3
#define CX_NET_BUFLEN (2 * MAX_PACKET_LEN)
#define CX_NET_INACTIVITY_TIMEOUT 10

typedef struct cx_net_client_t cx_net_client_t;
typedef struct cx_net_common_t cx_net_common_t;
typedef struct cx_net_ctx_sv_t cx_net_ctx_sv_t;
typedef struct cx_net_ctx_cl_t cx_net_ctx_cl_t;
typedef struct cx_net_args_t cx_net_args_t;

typedef struct sockaddr_in sockaddr_in;
typedef struct epoll_event epoll_event;

typedef char ipv4_t[16];

typedef void(*cx_net_handler_cb)(const cx_net_common_t* _common, void* _userData, const char* _data, uint16_t _size);

typedef bool(*cx_net_on_connection_cb)(cx_net_ctx_sv_t* _ctx, const ipv4_t _ipv4);
typedef void(*cx_net_on_disconnection_cb)(cx_net_ctx_sv_t* _ctx, cx_net_client_t* _client);

typedef void(*cx_net_on_connected_cb)(cx_net_ctx_cl_t* _ctx);
typedef void(*cx_net_on_disconnected_cb)(cx_net_ctx_cl_t* _ctx);

typedef enum CX_NET_PACKET
{
    CX_NETP_PING = 0,                               // sends a ping to keep the connection alive.
    CX_NETP_PONG,                                   // sends a pong (ping-reply) to keep the connection alive.
    _CX_NETP_BEGIN_,                                // end of reserved packets. begins the user-defined enum of packets.
    CX_NETP_AUTH,                                   // sends an authentication request.
    CX_NETP_ACK                                     // acknowledges an authentication.
} CX_NET_PACKET;

typedef enum
{
    CX_NET_STATE_NONE           = 0x0,              // default initial value.
    CX_NET_STATE_ERROR          = 0x1,              // there was an error in the process of listening/connecting.
    CX_NET_STATE_SERVER         = 0x2,              // the context is a server.
    CX_NET_STATE_CLIENT         = 0x4,              // the context is a client.
    CX_NET_STATE_LISTENING      = 0x8,              // the server is listening.
    CX_NET_STATE_CONNECTING     = 0x10,             // the client is attempting to connect to the given server.
    CX_NET_STATE_CONNECTED      = 0x20,             // the client is connected to the server.
    CX_NET_STATE_CLOSING        = 0x40,             // the context is being closed (running cx_net_close method).
} CX_NET_STATE;

struct cx_net_client_t
{
    uint16_t                    handle;             // handle/identifier for this client in our clients array.
    bool                        validated;          // true if this client was validated. if a client is not validated before the timeout is exceeded the connection is terminated.
    double                      connectedTime;      // time counter value of when the connection was established.
    double                      lastPacketTime;     // time counter value of when the last packet arrived.
    ipv4_t                      ip;                 // ipv4 string of the client.
    int32_t                     sock;               // file descriptor for communicating between client and server.
    char                        in[CX_NET_BUFLEN];  // pre-allocated buffer for inbound data.
    uint32_t                    inPos;              // current position in the inbound buffer.
    char                        out[CX_NET_BUFLEN]; // pre-allocated buffer for outbound data.
    uint32_t                    outPos;             // current position in the outbound buffer.
};

struct cx_net_common_t
{
    int32_t                     state;              // network context state. combination of CX_NET_STATE flags.
    char                        name[32];           // descriptive context name for debugging purposes.
    ipv4_t                      ip;                 // ipv4 string of either the server connected to or the listening ip address.
    uint16_t                    port;               // port number on which this socket is either listening on / connected to.
    int32_t                     sock;               // file descriptor for either the listening socket or the server socket (on a client context).
    double                      validationTimeout;  // maximum amount of time in seconds to wait before validation process times-out.
    int32_t                     errorNumber;        // number of the last error.
    cx_net_handler_cb           msgHandlers[256];   // callback containing a message handler for each message header supported.
    int32_t                     epollDescriptor;    // file descriptor to the epoll instance.
    epoll_event*                epollEvents;        // pre-allocated buffer for retrieving epoll events when calling epoll_wait.
    pthread_mutex_t             mtx;                // mutex for syncing cx_net_send.
    bool                        mtxInitialized;     // true if mtx is initialized.
};

struct cx_net_ctx_sv_t
{
    cx_net_common_t             c;                  // server context common data.
    cx_net_client_t*            clients;            // pre-allocated buffer for storing client's data.
    cx_handle_alloc_t*          clientsHalloc;      // handle allocator for clients array.
    uint16_t                    clientsMax;         // maximum amount of clients supported for this server context.
    uint16_t*                   tmpHandles;         // temporary pre-allocated buffer for storing client handles.
    cx_net_on_connection_cb     onConnection;       // event fired upon a client connection.
    cx_net_on_disconnection_cb  onDisconnection;    // event Fired upon a client disconnection.
};

struct cx_net_ctx_cl_t
{
    cx_net_common_t             c;                  // client context common data.
    bool                        validated;          // true if this client was validated. if a client is not validated before the timeout is exceeded the connection is terminated.
    double                      connectedTime;      // time counter value of when the connection was established.
    double                      lastPacketTime;     // time counter value of when the last packet arrived.
    char                        in[CX_NET_BUFLEN];  // pre-allocated buffer for inbound data.
    uint32_t                    inPos;              // current position in the inbound buffer.
    char                        out[CX_NET_BUFLEN]; // pre-allocated buffer for outbound data.
    uint32_t                    outPos;             // current position in the outbound buffer.
    cx_net_on_connected_cb      onConnected;        // event fired upon a connection with a remote server is established.
    cx_net_on_disconnected_cb   onDisconnected;     // event Fired upon a connection with a remote server is terminated.
};

typedef union cx_net_ctx_t
{
    cx_net_common_t*            c;                  // casts pointer as cx_net_common_t.
    cx_net_ctx_cl_t*            cl;                 // casts pointer as cx_net_ctx_cl_t.
    cx_net_ctx_sv_t*            sv;                 // casts pointer as cx_net_ctx_sv_t.
} cx_net_ctx_t;

struct cx_net_args_t
{
    char                        name[32];           // descriptive socket name for debugging purposes.
    ipv4_t                      ip;                 // ip address to either listen or connect to.
    uint16_t                    port;               // port to either listen or connect to.
    bool                        multiThreadedSend;  // true if this context must support multi-threaded send.
    cx_net_handler_cb           msgHandlers[256];   // callback containing a message handler for each message header supported.
    double                      validationTimeout;  // maximum amount of time in seconds to wait before validation process times-out.

                                                    // cx_net_ctx_cl_t arguments.
    bool                        connectBlocking;    // [ctx_cl] true if the client connect must be done synchronously.
    uint16_t                    connectTimeout;     // [ctx_cl] max amount of milliseconds to wait until a remote server connection is established.
    cx_net_on_connected_cb      onConnected;        // [ctx_cl] event fired upon a connection with a remote server is established.
    cx_net_on_disconnected_cb   onDisconnected;     // [ctx_cl] event Fired upon a connection with a remote server is terminated.

                                                    // cx_net_ctx_sv_t arguments.
    uint16_t                    maxClients;         // [ctx_sv] max amount of clients supported.
    cx_net_on_connection_cb     onConnection;       // [ctx_sv] event fired upon a client connection.
    cx_net_on_disconnection_cb  onDisconnection;    // [ctx_sv] event Fired upon a client disconnection.
};

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_net_ctx_sv_t*        cx_net_listen(cx_net_args_t* _args);

cx_net_ctx_cl_t*        cx_net_connect(cx_net_args_t* _args);

void                    cx_net_close(void* _ctx);

void                    cx_net_poll_events(void* _ctx, int32_t _timeout);

bool                    cx_net_send(void* _ctx, uint8_t _header, const char* _payload, uint32_t _payloadSize, uint16_t _clientHandle);

void                    cx_net_validate(void* _ctx, uint16_t _clientHandle);

bool                    cx_net_flush(void* _ctx, uint16_t _clientHandle);

void                    cx_net_disconnect(void* _ctx, uint16_t _clientHandle, const char* _reason);

#endif // CX_NET_H_

