#ifndef GOSSIP_H_
#define GOSSIP_H_

#include <ker/defines.h>
#include <cx/cdict.h>

#include <stdbool.h>

typedef enum GOSSIP_STAGE
{
    GOSSIP_STAGE_NONE = 0,
    GOSSIP_STAGE_HANDSHAKING,
    GOSSIP_STAGE_ACKNOWLEDGED,
    GOSSIP_STAGE_REQUESTING,
    GOSSIP_STAGE_DONE,
} GOSSIP_STAGE;

typedef struct gossip_node_t
{
    uint16_t            number;                             // MEM node number (unique identifier in the pool assigned to each node).
    bool                available;                          // true if this node was available during the previous gossip.
    ipv4_t              ip;                                 // ip address on which the MEM node will listen.
    uint16_t            port;                               // tcp port on which the MEM node will listen.
    cx_net_ctx_cl_t*    conn;                               // client context for communicating with the MEM node.
    GOSSIP_STAGE        stage;                              // current stage of the gossip process (see GOSSIP_STAGE enum).
    double              lastGossipTime;                     // value of cx_time_counter of the last successful gossip.
} gossip_node_t;

typedef struct gossip_ctx_t
{
    cx_cdict_t*         nodes;                              // table with current MEM nodes (mem_node_t*) available indexed by ipv4:port.
                                                            // <-- this dictionary doesn't really have to be thread-safe (TODO: implement normal dict)
} gossip_ctx_t;

typedef bool(*gossip_func_cb)(gossip_node_t* _memNode, void* _userData);

typedef char node_key_t[sizeof(ipv4_t) + 1 + 5];

#define MEM_NUMBER_UNKNOWN 0

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool        gossip_init(const seed_t* _seeds, uint16_t _seedsCount, cx_err_t* _err);

void        gossip_destroy();

void        gossip_add(const ipv4_t _ip, uint16_t _port, uint16_t _memNumber);

void        gossip_run();

void        gossip_poll_events();

void        gossip_export(payload_t* _payload, uint32_t* _payloadSize);

void        gossip_import(payload_t* _payload, uint32_t _payloadSize);

#endif // GOSSIP_H_