#ifndef KER_H_
#define KER_H_

#include <ker/defines.h>
#include <cx/cx.h>

#include <commons/config.h>

typedef enum KER_TIMER
{
    KER_TIMER_METAREFRESH = 0,
    KER_TIMER_COUNT
} KER_TIMER;

typedef struct
{
    t_config*           handle;                     // pointer to so-commons-lib config adt.
    uint16_t            workers;                    // number of worker threads to spawn to process requests.
    uint8_t             quantum;                    // maximum amount of lines of LQL scripts to run sequentially without returning control back to scheduler.
    ipv4_t              memIp;                      // ip address to connect to a MEM node.
    uint16_t            memPort;                    // tcp port to connect to a MEM node.
    password_t          memPassword;                // password to authenticate with the MEM node.
    uint32_t            delayRun;                   // LQL line run delay in milliseconds.
    uint32_t            intervalMetaRefresh;        // interval in milliseconds to perform the metadata refresh process.
} cfg_t;

typedef struct
{
    cfg_t               cfg;                        // mem node configuration data.
    bool                isRunning;                  // true if the server is running. false if it's shutting down.
    payload_t           buff1;                      // temporary pre-allocated buffer for building packets.
    uint16_t            timerMetaRefresh;           // cx timer handle for running the journaling operation.
    char*               shutdownReason;             // reason that caused this KER node to exit.
} ker_ctx_t;

extern ker_ctx_t        g_ctx;

#endif // KER_H_
