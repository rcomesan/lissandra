#ifndef CX_CLI_H_
#define CX_CLI_H_

#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h>

#define CX_CLI_MAX_CMD_LEN 32
#define CX_CLI_MAX_CMD_ARGS 8

typedef enum
{
    CX_CLI_STATE_NONE = 0,                              // default state.
    CX_CLI_STATE_READING,                               // reading from stdin to get the next line entered.
    CX_CLI_STATE_PENDING,                               // waiting for the main thread to pick-up a pending command.
    CX_CLI_STATE_PROCESSING,                            // waiting for the main thread to process this command and print the result.
    CX_CLI_STATE_SHUTDOWN,                              // the cli thread is shutting down.
} CX_CLI_STATE;

typedef enum
{
    CX_CLI_PARSING_HEADER = 0,                          // we're parsing the header of the command (first token).
    CX_CLI_PARSING_TOKEN = 1,                           // we're parsing a normal token without quotes (can contain letters, digits, special chars except spaces and quotes).
    CX_CLI_PARSING_STRING = 2,                          // we're parsing a quote-enclosed string.
} CX_CLI_PARSING_STAGE;

typedef struct cx_cli_cmd_t
{
    char                header[CX_CLI_MAX_CMD_LEN+1];   // first token (identifier of the command).
    char*               args[CX_CLI_MAX_CMD_ARGS];      // array of strings for storing arguments.
    uint8_t             argsCount;                      // number of arguments stored for this command.
} cx_cli_cmd_t;

typedef struct cx_cli_ctx_t
{
    bool                initialized;                    // true if this cli context is initialized (second thread running in background reading from stdin).
    CX_CLI_STATE        state;                          // state of our command line interface. any of CX_CLI_STATE defined states.
    cx_cli_cmd_t        command;                        // the current command that is being processed/waiting to be processed.
    pthread_t           thread;                         // pthread instance adt.
    sem_t               sem;                            // semaphore for syncing reading/processing commands.
    bool                semInitialized;                 // true if the semaphore was successfully initialized.
    pthread_mutex_t     mtx;                            // mutex for protecting state variable in critical sections.
    bool                mtxInitialized;                 // true if the mutex was successfully initialized.
} cx_cli_ctx_t;


/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool                    cx_cli_init();

void                    cx_cli_destroy();

bool                    cx_cli_command_begin(cx_cli_cmd_t** _outCmd);

void                    cx_cli_command_end();

void                    cx_cli_cmd_parse(const char* _cmd, cx_cli_cmd_t* _outCmd);

void                    cx_cli_cmd_destroy(cx_cli_cmd_t* _outCmd);

#endif // CX_CLI_H_