#include "cx.h"
#include "str.h"
#include "file.h"
#include "timer.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

static char         g_projectName[64] = "undefined";
static cx_path_t    g_logFilePath;
static FILE*        g_logFile = NULL;

bool cx_init(const char* _projectName, bool _logFile, const char* _logFilePath, cx_err_t* _err)
{
    cx_str_copy(g_projectName, sizeof(g_projectName), _projectName);
    
#if CX_DEBUG || CX_RELEASE_VERBOSE
    if (_logFile)
    {
        if (NULL == _logFilePath)
        {
            cx_timestamp_t timestamp;
            cx_time_stamp(&timestamp);
            cx_file_path(&g_logFilePath, "logs/%s.txt", timestamp);
        }
        else
        {
            cx_file_path(&g_logFilePath, "%s", _logFilePath);
        }

        if (cx_file_touch(&g_logFilePath, _err))
        {
            g_logFile = fopen(g_logFilePath, "w");
            if (NULL != g_logFile) return true;

            CX_ERR_SET(_err, 1, "%s log file open failed (%s). %s.", g_projectName, g_logFilePath, strerror(errno));
        }
        else
        {
            CX_ERR_SET(_err, 1, "%s log file touch failed (%s).", g_projectName, g_logFilePath);
        }

        return false;
    }
    else
    {
        return true;
    }
#else
    return true;
#endif
}

void cx_destroy()
{
    if (g_logFile != NULL)
    {
        fclose(g_logFile);
        g_logFile = NULL;
    }
}

char* cx_logfile()
{
    if (NULL != g_logFile)
    {
        return g_logFilePath;
    }
    else
    {
        return NULL;
    }
}

void cx_trace(const char* _sourceFile, uint16_t _lineNumber, const char* _format, ...)
{
    char temp[2048];
    char* out = temp;

    va_list args;
    va_start(args, _format);

    // format the header of the trace, the returned value is the strlen of the string (not counting the terminating null char)
    uint32_t sourceLen = snprintf(temp, sizeof(temp), "[%s] [%s:%d] ", g_projectName, _sourceFile, _lineNumber);

    // vsnprintf writes at most n-1 bytes. account one extra byte to make space for the new line character
    uint32_t totalLen = sourceLen + vsnprintf(temp + sourceLen, sizeof(temp) - sourceLen - 1, _format, args) + 1;

    if (totalLen > sizeof(temp))
    {
        out = malloc(totalLen + 1);

        if (NULL != out)
        {
            memcpy(out, temp, sourceLen);
            vsnprintf(out + sourceLen, totalLen - sourceLen + 1, _format, args);
        }
        else
        {
            // not enough heap memory, truncate. 
            out = temp;
            totalLen = sizeof(temp);
        }
    }

    out[totalLen - 1] = '\n';
    out[totalLen + 0] = '\0';

#if CX_DEBUG
    fputs(out, stdout);
#endif

#if CX_DEBUG || CX_RELEASE_VERBOSE
    if (NULL != g_logFile)
    {
        fputs(out, g_logFile);
        fflush(g_logFile);
    }
#endif

    if (out != temp)
    {
        free(out);
    }

    va_end(args);
}
