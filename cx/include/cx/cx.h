#ifndef CX_CX_H_
#define CX_CX_H_

#include <stdint.h>
#include <stdlib.h>

#ifdef DEBUG
#include <signal.h>
#define CX_DEBUG 1
#define DEBUG_BREAK raise(SIGINT)
#define CX_ALW 0
#else
#define CX_DEBUG 0
#endif

void cx_init(const char* _projectName);

void cx_trace(const char* _filePath, uint16_t _lineNumber, const char* _format, ...);
    
#define CX_MACRO_BLOCK_BEGIN do {
#define CX_MACRO_BLOCK_END } while (0);
#define CX_NOOP(...) CX_MACRO_BLOCK_BEGIN CX_MACRO_BLOCK_END

#if CX_DEBUG
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
        CX_CHECK(_var, #_var " can't be NULL!")

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