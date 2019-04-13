#include "cx.h"
#include "str.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>

static char     g_projectName[64] = "undefined";
static int64_t  g_timeOffset;
static double   g_timeCounter;
static double   g_timeCounterPrev;
static double   g_timeDelta;

void cx_init(const char* _projectName)
{
    cx_str_copy(g_projectName, sizeof(g_projectName), _projectName);

    struct timeval now;
    gettimeofday(&now, 0);
    g_timeOffset = now.tv_sec * INT64_C(1000000) + now.tv_usec;
}

void cx_trace(const char * _filePath, uint16_t _lineNumber, const char * _format, ...)
{
    char temp[2048];
    char* out = temp;

    va_list args;
    va_start(args, _format);

    // format the header of the trace, the returned value is the strlen of the string (not counting the terminating null char)
    uint32_t sourceLen = snprintf(temp, sizeof(temp), "[%s] [%s:%d] ", g_projectName, _filePath, _lineNumber);

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
    fputs(out, stdout);

    if (out != temp)
    {
        free(out);
    }

    va_end(args);
}

double cx_time_counter()
{
    return g_timeCounter;
}

double cx_time_delta()
{
    return g_timeDelta;
}

void cx_time_update()
{
    struct timeval now;
    gettimeofday(&now, 0);

    int64_t time = now.tv_sec * INT64_C(1000000) + now.tv_usec;
    g_timeCounter = (double)(time - g_timeOffset) / (double)INT64_C(1000000);
    g_timeDelta = g_timeCounter - g_timeCounterPrev;
    g_timeCounterPrev = g_timeCounter;
}

uint32_t cx_time_stamp()
{
    //TODO CHECKME is seconds precision OK for this?
    return time(NULL);
}