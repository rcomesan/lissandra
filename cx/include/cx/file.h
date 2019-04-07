#ifndef CX_FILE_H_
#define CX_FILE_H_

#include <stdio.h>
#include <stdint.h>

#define MAX_FILE_PATH_LEN 2048

typedef enum
{
    CX_FILE_OPEN_NONE,     // Default. Closed file.
    CX_FILE_OPEN_READ,     // Opens a file for reading. The file must exist.
    CX_FILE_OPEN_WRITE,    // Creates an empty file for writing. If a file with the same name already exists, its content is erased and the file is considered as a new empty file.
    CX_FILE_OPEN_APPEND,   // Appends to a file. Writing operations, append data at the end of the file. The file is created if it does not exist.
    CX_FILE_OPEN_COUNT
} CX_FILE_OPEN_MODE;

typedef struct file_t
{
    char path[MAX_FILE_PATH_LEN];   // Path to the opened file.
    FILE* handle;                   // Pointer to the opened file handle.
    CX_FILE_OPEN_MODE mode;         // The io mode intended for this file.
    uint32_t lastLineNumber;        // Previous line number (starting at 1). Cached for fast sequential read operations.
    uint32_t lastLinePos;           // Previous line position in the file. Cached for fast sequential read operations.
} cx_file_t;

typedef void (*cx_file_for_each_cb)(const char* _str, void* _passThru);
typedef char* (*cx_file_map_cb)(const char* _str, void* _passThru);

cx_file_t*  cx_file_open(const char* _filePath, CX_FILE_OPEN_MODE _mode);

void        cx_file_close(cx_file_t* _file);

void        cx_file_delete(cx_file_t* _file);

int32_t     cx_file_line_read(cx_file_t* _file, uint32_t _number, char* _buffer, uint32_t _bufferSize);

void        cx_file_line_append(cx_file_t* _file, char* _content);

void        cx_file_lines_for_each(cx_file_t* _file, cx_file_for_each_cb _cb, void* _passThrou);

void        cx_file_lines_map(cx_file_t** _file, cx_file_map_cb _cb, void* _passThrou);

#endif // CX_FILE_H_