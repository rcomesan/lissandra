#ifndef CX_CX_H_
#define CX_CX_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#define CX_MACRO_BLOCK_BEGIN do {
#define CX_MACRO_BLOCK_END } while (0);
#define CX_NOOP(...) CX_MACRO_BLOCK_BEGIN CX_MACRO_BLOCK_END

#if defined(DEBUG) || defined(CX_DEBUG)
#include <signal.h>
#undef  CX_DEBUG
#define CX_DEBUG 1
#define DEBUG_BREAK raise(SIGINT)
#endif

#ifndef CX_DEBUG
#define CX_DEBUG 0
#endif

#ifndef CX_RELEASE_VERBOSE
#define CX_RELEASE_VERBOSE 0
#endif 

#ifndef DEBUG_BREAK
#define DEBUG_BREAK CX_NOOP()
#endif

#define INVALID_DESCRIPTOR -1
#define CX_ALW 0

#define CX_TIMESTAMP_LEN 14
typedef char cx_timestamp_t[CX_TIMESTAMP_LEN + 1];

#define CX_ERR_LEN 4095
typedef struct cx_err_t
{
    uint32_t    code;
    char        desc[CX_ERR_LEN + 1];
} cx_err_t;

typedef void(*cx_destroyer_cb)(void* _data);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool            cx_init(const char* _projectName, bool _logFile, const char* _logFilePath, cx_err_t* _err);

void            cx_destroy();

char*           cx_logfile();

void            cx_trace(const char* _filePath, uint16_t _lineNumber, const char* _format, ...);

/****************************************************************************************
 ***  MACROS
 ***************************************************************************************/

#define CX_UNUSED(var)                                                                  \
    CX_MACRO_BLOCK_BEGIN                                                                \
        (void)(var);                                                                    \
    CX_MACRO_BLOCK_END

#define ERR_NONE 0

#define CX_ERR_CLEAR(_errPtr)                                                           \
    if (NULL != (_errPtr))                                                              \
    {                                                                                   \
        memset((_errPtr), 0, sizeof((*_errPtr)));                                       \
    }

#define CX_ERR_SET(_err, _code, _format, ...)                                           \
    if (NULL != (_err))                                                                 \
    {                                                                                   \
        (_err)->code = _code;                                                           \
        snprintf((_err)->desc, sizeof((_err)->desc), _format, ##__VA_ARGS__);           \
    }

#if CX_DEBUG || CX_RELEASE_VERBOSE
    #define CX_INFO(_format, ...)                                                        \
        cx_trace(__FILE__, (uint16_t)__LINE__, "[INFO] " _format, ##__VA_ARGS__)

    #define CX_WARN(_condition, _format, ...)                                            \
        if (!(_condition))                                                               \
        {                                                                                \
            cx_trace(__FILE__, (uint16_t)__LINE__, "[WARN] " _format, ##__VA_ARGS__);    \
        }

    #define CX_CHECK(_condition, _format, ...)                                           \
        if (!(_condition))                                                               \
        {                                                                                \
            cx_trace(__FILE__, (uint16_t)__LINE__, "[CHECK] " _format, ##__VA_ARGS__);   \
            DEBUG_BREAK;                                                                 \
        }
        
    #define CX_CHECK_NOT_NULL(_var)                                                      \
        CX_CHECK(NULL != (_var), #_var " can't be NULL!")

    #define CX_FATAL(_condition, _format, ...)                                           \
        if (!(_condition))                                                               \
        {                                                                                \
            cx_trace(__FILE__, (uint16_t)__LINE__, "[FATAL] " _format, ##__VA_ARGS__);   \
            abort();                                                                     \
        }

#else
    #define CX_INFO(...) CX_NOOP()
    #define CX_WARN(...) CX_NOOP()
    #define CX_CHECK(...) CX_NOOP()
    #define CX_CHECK_NOT_NULL(...) CX_NOOP()
    #define CX_FATAL(...) CX_NOOP()
#endif

#endif // CX_CX_H_