#include "cli.h"

#include <cx.h>
#include <mem.h>
#include <str.h>
#include <math.h>

#include <commons/string.h>

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <readline/readline.h>
#include <pthread.h>

static cx_cli_ctx_t*    m_cliCtx = NULL;    // private cli context

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void*            cx_cli_main_loop(void* _arg);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool cx_cli_init()
{
    if (NULL == m_cliCtx)
        m_cliCtx = CX_MEM_STRUCT_ALLOC(m_cliCtx);

    CX_CHECK(!m_cliCtx->initialized, "cli module is already initialized!");
    if (m_cliCtx->initialized) return false;

    m_cliCtx->initialized = false;
    m_cliCtx->state = CX_CLI_STATE_NONE;

    m_cliCtx->mtxInitialized = (0 == pthread_mutex_init(&m_cliCtx->mtx, NULL));
    if (m_cliCtx->mtxInitialized)
    {
        m_cliCtx->semInitialized = (0 == sem_init(&m_cliCtx->sem, 0, 0));
        if (m_cliCtx->semInitialized)
        {
            int32_t result = pthread_create(&m_cliCtx->thread, NULL, cx_cli_main_loop, NULL);
            if (0 == result)
            {
                m_cliCtx->initialized = true;

                // send the signal to start consuming commands from stdin.
                pthread_mutex_lock(&m_cliCtx->mtx);
                m_cliCtx->state = CX_CLI_STATE_READING;
                sem_post(&m_cliCtx->sem);
                pthread_mutex_unlock(&m_cliCtx->mtx);
            }
            CX_WARN(0 == result, "cx command line interface thread creation failed: %s", strerror(errno));
        }
        CX_WARN(m_cliCtx->semInitialized, "cx command line interface semaphore creation failed: %s", strerror(errno));
    }
    CX_WARN(m_cliCtx->mtxInitialized, "cx command line interface mutex creation failed: %s", strerror(errno));

    return m_cliCtx->initialized;
}

void cx_cli_destroy()
{
    if (NULL == m_cliCtx || !m_cliCtx->initialized) return;

    pthread_mutex_lock(&m_cliCtx->mtx);
    if (CX_CLI_STATE_READING == m_cliCtx->state)
    {
        m_cliCtx->state = CX_CLI_STATE_SHUTDOWN;
        pthread_cancel(m_cliCtx->thread);
    }
    else if (CX_CLI_STATE_PROCESSING == m_cliCtx->state)
    {
        m_cliCtx->state = CX_CLI_STATE_SHUTDOWN;
        sem_post(&m_cliCtx->sem);
    }
    else
    {
        m_cliCtx->state = CX_CLI_STATE_SHUTDOWN;
    }
    pthread_mutex_unlock(&m_cliCtx->mtx);

    // wait for completion
    pthread_join(m_cliCtx->thread, NULL);
    
    if (m_cliCtx->semInitialized)
    {
        sem_destroy(&m_cliCtx->sem);
        m_cliCtx->semInitialized = false;
    }   

    if (m_cliCtx->mtxInitialized)
    {
        pthread_mutex_destroy(&m_cliCtx->mtx);
        m_cliCtx->mtxInitialized = false;
    }

    // destroy our context
    free(m_cliCtx);
    m_cliCtx = NULL;
}

bool cx_cli_command_begin(cx_cli_cmd_t** _outCmd)
{
    bool available = false;

    pthread_mutex_lock(&m_cliCtx->mtx);
    if (CX_CLI_STATE_PENDING == m_cliCtx->state)
    {
        m_cliCtx->state = CX_CLI_STATE_PROCESSING;
        (*_outCmd) = &(m_cliCtx->command);

        available = true;
    }
    pthread_mutex_unlock(&m_cliCtx->mtx);

    return available;
}

void cx_cli_command_end()
{
    pthread_mutex_lock(&m_cliCtx->mtx);
    if (CX_CLI_STATE_PROCESSING == m_cliCtx->state)
    {
        // free the dynamically allocated strings
        for (int32_t i = 0; i < m_cliCtx->command.argsCount; i++)
        {
            free(m_cliCtx->command.args[i]);
        }
        CX_MEM_ZERO(m_cliCtx->command);

        // resume reading from stdin
        m_cliCtx->state = CX_CLI_STATE_READING;
        sem_post(&m_cliCtx->sem);
    }
    pthread_mutex_unlock(&m_cliCtx->mtx);
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static void* cx_cli_main_loop(void* _arg)
{
    char*       cmd = NULL;
    uint32_t    cmdLen = 0;
    char        temp[4096];

    CX_INFO("command line interface loop started.");

    pthread_mutex_lock(&m_cliCtx->mtx);
    while (CX_CLI_STATE_SHUTDOWN != m_cliCtx->state)
    {
        pthread_mutex_unlock(&m_cliCtx->mtx);
        sem_wait(&m_cliCtx->sem);
        pthread_mutex_lock(&m_cliCtx->mtx);

        if (CX_CLI_STATE_READING == m_cliCtx->state)
        {
            pthread_mutex_unlock(&m_cliCtx->mtx);
            cmd = readline(">>> ");
            pthread_mutex_lock(&m_cliCtx->mtx);

            cmdLen = strlen(cmd);

            if (NULL != cmd)
            {
                CX_CLI_PARSING_STAGE stage = CX_CLI_PARSING_HEADER;
                bool endArgument = false;
                uint16_t pos = 0;
                
                for (uint32_t i = 0; i < cmdLen; i++)
                {
                    if (' ' == cmd[i])
                    {
                        if (CX_CLI_PARSING_STRING == stage)
                        {
                            temp[pos++] = cmd[i];
                        } 
                        else if ((CX_CLI_PARSING_HEADER == stage || CX_CLI_PARSING_TOKEN == stage) && pos > 0)
                        {
                            endArgument = true;
                        }
                    }
                    else if ('"' == cmd[i])
                    {
                        if (CX_CLI_PARSING_STRING == stage)
                        {
                            endArgument = true;
                        }
                        else if (CX_CLI_PARSING_TOKEN == stage && pos == 0)
                        {
                            // the first char of the token is a quote: this is, in fact, a string
                            stage = CX_CLI_PARSING_STRING;
                        }
                    }
                    else
                    {
                        temp[pos++] = cmd[i];
                    }

                    if ((endArgument || (i == cmdLen - 1)) && pos > 0)
                    {
                        endArgument = false;
                        temp[pos] = '\0';

                        if (m_cliCtx->command.argsCount < CX_CLI_MAX_CMD_ARGS)
                        {
                            if (CX_CLI_PARSING_HEADER == stage)
                            {
                                uint8_t headerLen = cx_math_min(pos, CX_CLI_MAX_CMD_LEN);
                                CX_WARN(pos <= CX_CLI_MAX_CMD_LEN, "truncated command header (max is %d)", CX_CLI_MAX_CMD_LEN);

                                cx_str_copy(m_cliCtx->command.header, headerLen + 1, temp);
                                string_to_upper(m_cliCtx->command.header);
                            }
                            else
                            {
                                m_cliCtx->command.args[m_cliCtx->command.argsCount] = cx_str_copy_d(temp);
                                m_cliCtx->command.argsCount++;
                            }
                        }
                        else
                        {
                            CX_WARN(CX_ALW, "out of arguments buffer space. (max is %d)", CX_CLI_MAX_CMD_ARGS);
                            break;
                        }

                        // start parsing a new token
                        stage = CX_CLI_PARSING_TOKEN;
                        pos = 0;
                    }
                }

                free(cmd);
                m_cliCtx->state = CX_CLI_STATE_PENDING;
            }
        }
    }
    pthread_mutex_unlock(&m_cliCtx->mtx);

    CX_INFO("command line interface loop terminated gracefully.");

    return 0;
}
