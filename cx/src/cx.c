#include "cx.h"
#include "str.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static char g_projectName[64] = "undefined";

void cx_init(const char* _projectName)
{
    cx_str_copy(g_projectName, sizeof(g_projectName), _projectName);
}

void cx_trace(const char * _filePath, uint16_t _lineNumber, const char * _format, ...)
{
    char temp[2048];
    char* out = temp;

    va_list args;
    va_start(args, _format);

    uint32_t sourceLen = snprintf(temp, sizeof(temp), "[%s] [%s:%d] ", g_projectName, _filePath, _lineNumber);
    uint32_t totalLen = sourceLen + vsnprintf(temp + sourceLen, sizeof(temp) - sourceLen, _format, args);

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

    out[totalLen] = '\0';
    fputs(out, stdout);
    fputs("\n", stdout);
    fflush(stdout);

    if (out != temp)
    {
        free(out);
    }

    va_end(args);
}