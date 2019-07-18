#ifndef KER_H_
#define KER_H_

#include <ker/defines.h>
#include <cx/cx.h>

#include <commons/config.h>

#define KER_CFG_WORKERS         "workers"
#define KER_CFG_QUANTUM         "quantum"
#define KER_CFG_MEM_NUMBER      "memNumber"
#define KER_CFG_MEM_IP          "memIp"
#define KER_CFG_MEM_PORT        "memPort"
#define KER_CFG_MEM_PASSWORD    "memPassword"
#define KER_CFG_DELAY_RUN       "delayRun"
#define KER_CFG_INT_METAREFRESH "intervalMetaRefresh"
#define KER_CFG_INT_GOSSIPING   "intervalGossiping"

typedef enum KER_TIMER
{
    KER_TIMER_METAREFRESH = 0,
    KER_TIMER_GOSSIP,
    KER_TIMER_METRICS,
    KER_TIMER_COUNT
} KER_TIMER;

typedef struct
{
    uint16_t            workers;                    // number of worker threads to spawn to process requests.
    uint8_t             quantum;                    // maximum amount of lines of LQL scripts to run sequentially without returning control back to scheduler.
    seed_t              seed;                       // single MEM node known by KER on startup (seed).
    password_t          memPassword;                // password to authenticate with MEM nodes.
    uint32_t            delayRun;                   // LQL line run delay in milliseconds.
    uint32_t            intervalMetaRefresh;        // interval in milliseconds to perform the metadata refresh process.
    uint32_t            intervalGossiping;          // interval in milliseconds to perform the gossiping process.
} cfg_t;

typedef struct
{
    cfg_t               cfg;                        // mem node configuration data.
    bool                cfgInitialized;             // true if the config was initialized (file path generated + full load).
    cx_path_t           cfgFilePath;                // configuration file path.
    uint16_t            cfgFswatchHandle;           // cx fswatch handle for reloading the config file.
    bool                isRunning;                  // true if the server is running. false if it's shutting down.
    payload_t           buff1;                      // temporary pre-allocated buffer for building packets.
    uint16_t            timerMetaRefresh;           // cx timer handle for running the metadata refresh operation.
    uint16_t            timerGossip;                // cx timer handle for running the gossiping operation.
    uint16_t            timerMetrics;               // cx timer handle for re-sampling mempool metrics.
    char*               shutdownReason;             // reason that caused this KER node to exit.
} ker_ctx_t;

extern ker_ctx_t        g_ctx;

#endif // KER_H_
