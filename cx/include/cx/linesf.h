#ifndef CX_LINESF_H_
#define CX_LINESF_H_

#include "cx.h"
#include "file.h"

#include <stdio.h>
#include <stdint.h>
#include <limits.h>

typedef enum
{
    CX_LINESF_OPEN_NONE = 0,                // Default. Closed file.
    CX_LINESF_OPEN_READ,                    // Opens a file for reading. The file must exist.
    CX_LINESF_OPEN_WRITE,                   // Creates an empty file for writing. If a file with the same name already exists, its content is erased and the file is considered as a new empty file.
    CX_LINESF_OPEN_APPEND,                  // Appends to a file. Writing operations, append data at the end of the file. The file is created if it does not exist.
    CX_LINESF_OPEN_COUNT
} CX_LINESF_OPEN_MODE;

typedef struct
{
    cx_path_t           path;               // Path to the opened file.
    FILE*               handle;             // Pointer to the opened file handle.
    CX_LINESF_OPEN_MODE mode;               // The io mode intended for this file.
    uint32_t            lastLineNumber;     // Previous line number (starting at 1). Cached for fast sequential read operations.
    uint32_t            lastLinePos;        // Previous line position in the file. Cached for fast sequential read operations.
} cx_linesf_t;

typedef void (*cx_linesf_for_each_cb)(const char* _str, void* _userData);
typedef char* (*cx_linesf_map_cb)(const char* _str, void* _userData);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_linesf_t*            cx_linesf_open(const char* _filePath, CX_LINESF_OPEN_MODE _mode, cx_err_t* _err);

void                    cx_linesf_close(cx_linesf_t* _file);

void                    cx_linesf_delete(cx_linesf_t* _file);

int32_t                 cx_linesf_line_read(cx_linesf_t* _file, uint32_t _number, char* _buffer, uint32_t _bufferSize);

void                    cx_linesf_line_append(cx_linesf_t* _file, char* _content);

void                    cx_linesf_lines_for_each(cx_linesf_t* _file, cx_linesf_for_each_cb _cb, void* _userData);

void                    cx_linesf_lines_map(cx_linesf_t** _file, cx_linesf_map_cb _cb, void* _userData);

#endif // CX_LINESF_H_