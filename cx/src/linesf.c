#include "linesf.h"

#include "cx.h"
#include "mem.h"
#include "str.h"
#include "math.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <commons/string.h>

cx_linesf_t* cx_linesf_open(const char* _filePath, CX_LINESF_OPEN_MODE _mode, cx_err_t* _err)
{
    CX_CHECK(!cx_str_is_empty(_filePath), "_filePath can't be empty");

    cx_linesf_t* file = NULL;
    
    char* mode = "";
    switch (_mode)
    {
    case CX_LINESF_OPEN_READ:    mode = "r"; break;
    case CX_LINESF_OPEN_WRITE:   mode = "w"; break;
    case CX_LINESF_OPEN_APPEND:  mode = "a"; break;
    default:
        CX_ERR_SET(_err, 1, "Undefined mode %d when opening file: %s.", _mode, _filePath);
        return NULL;
    }    
    cx_path_t path;
    cx_file_path(&path, "%s", _filePath);
    cx_file_touch(&path, NULL);
    
    FILE* fileHandle = fopen(path, mode);
    if (NULL == fileHandle)
    {
        CX_ERR_SET(_err, 1, "File '%s' does not exist, or is not accessible.", path);
        CX_WARN(CX_ALW, "File '%s' does not exist, or is not accessible. %s.", path, strerror(errno));
    }
    else
    {
        file = CX_MEM_STRUCT_ALLOC(file);

        cx_str_copy(file->path, sizeof(file->path), path);
        file->handle = fileHandle;
        file->mode = _mode;
        file->lastLinePos = 0;
        file->lastLineNumber = 0;
    }

    return file;
}

void cx_linesf_close(cx_linesf_t* _file)
{
    CX_CHECK_NOT_NULL(_file);
    
    fclose(_file->handle);
    free(_file);
}

void cx_linesf_delete(cx_linesf_t* _file)
{
    CX_CHECK_NOT_NULL(_file);

    char filePath[PATH_MAX];
    memcpy(filePath, _file->path, PATH_MAX);

    cx_linesf_close(_file);
    remove(filePath);
}

int32_t cx_linesf_line_read(cx_linesf_t* _file, uint32_t _number, char* _buffer, uint32_t _bufferSize)
{
    CX_CHECK_NOT_NULL(_file);
    CX_CHECK_NOT_NULL(_buffer);
    CX_CHECK(CX_LINESF_OPEN_READ == _file->mode, "_file must be opened as FILE_OPEN_READ");
    CX_CHECK(_number > 0, "_number must be greater than zero");
    CX_CHECK(_bufferSize > 0, "_bufferSize must be greater than zero");

    if (_file->lastLineNumber < _number)
    {
        _file->lastLineNumber = 0;
        _file->lastLinePos = 0;
    }

    uint32_t i = 0;
    uint32_t bytesWritten = 0;
    fseek(_file->handle, _file->lastLinePos, SEEK_SET);

    while (_file->lastLineNumber < _number)
    {
        if (NULL != fgets(_buffer, _bufferSize, _file->handle))
        {
            // find the new line character
            for (i = 0; i < _bufferSize; i++)
                if ('\n' == _buffer[i]) break;
            
            if (i == _bufferSize)
            {
                CX_WARN(CX_ALW, "The given buffer (%d bytes) is not large enough to fully read the next line number " \
                    " (%d) from file %s", _bufferSize, _file->lastLineNumber + 1, _file->path);
                return -_bufferSize;
            }
            else
            {
                bytesWritten = i;
                _file->lastLineNumber++;
            }            
        }
        else
        {
            //CX_WARN(CX_ALW, "EOF Reached. File %s does not contain the requested line number %d", _file->path, _number);
            bytesWritten = 0;
            break;
        }
    }

    // get rid of the \n, we don't want it
    if (bytesWritten > 0)
        _buffer[bytesWritten] = '\0';

    _file->lastLinePos = ftell(_file->handle);
    return bytesWritten;
}

void cx_linesf_line_append(cx_linesf_t* _file, char* _content)
{
    CX_CHECK_NOT_NULL(_file);
    CX_CHECK(CX_LINESF_OPEN_WRITE == _file->mode || CX_LINESF_OPEN_APPEND == _file->mode,
        "The file must be opened as FILE_OPEN_WRITE or FILE_OPEN_APPEND");

    fputs(_content, _file->handle);
    fputc('\n', _file->handle);
}

void cx_linesf_lines_for_each(cx_linesf_t* _file, cx_linesf_for_each_cb _cb, void* _userData)
{
    CX_CHECK_NOT_NULL(_file);

    uint32_t lineNumber = 1;
    int32_t lineLength = 0;
    uint32_t bufferSize = 1024;
    char* buffer = malloc(bufferSize);

    while (1)
    {
        lineLength = cx_linesf_line_read(_file, lineNumber, buffer, bufferSize);

        if (lineLength == 0)
        {
            // eof reached
            break;
        }
        else if (lineLength < 0)
        {
            // the given buffer is not enough, let's try again
            bufferSize *= 2;
            buffer = realloc(buffer, bufferSize);
            if (NULL == buffer)
            {
                CX_WARN(CX_ALW, "Reallocation of %d bytes failed possibly due to oom. Aborting for each at line number %.", bufferSize, lineNumber);
                break;
            }
        }
        else
        {
            // a fully line was read from the file, run our callback
            _cb(buffer, _userData);
            lineNumber++;
        }
    }

    free(buffer);
}

void cx_linesf_lines_map(cx_linesf_t** _file, cx_linesf_map_cb _cb, void* _userData)
{
    CX_CHECK_NOT_NULL(_file);

    uint32_t lineNumber = 1;
    int32_t lineLength = 0;
    char* lineModified;

    char* inputFilePath = strdup((*_file)->path);
    char* outputFilePath = cx_str_cat_d((*_file)->path, ".new");
    cx_linesf_t* output = cx_linesf_open(outputFilePath, CX_LINESF_OPEN_WRITE, NULL);

    uint32_t bufferSize = 1024;
    char* buffer = malloc(bufferSize);

    while (1)
    {
        lineLength = cx_linesf_line_read(*_file, lineNumber, buffer, bufferSize);

        if (lineLength == 0)
        {
            // eof reached
            break;
        }
        else if (lineLength < 0)
        {
            // the given buffer is not enough, let's try again
            bufferSize *= 2;
            buffer = realloc(buffer, bufferSize);
            if (NULL == buffer)
            {
                CX_WARN(CX_ALW, "Reallocation of %d bytes failed possibly due to oom. Aborting map at line number %.", bufferSize, lineNumber);
                break;
            }
        }
        else
        {
            // a fully line was read from the file, run our callback and write it back to the output stream
            lineModified = _cb(buffer, _userData);
            cx_linesf_line_append(output, lineModified);
            free(lineModified);

            lineNumber++;
        }
    }

    cx_linesf_delete(*_file);
    cx_linesf_close(output);
    
    rename(outputFilePath, inputFilePath);
    (*_file) = cx_linesf_open(inputFilePath, CX_LINESF_OPEN_READ, NULL);

    free(inputFilePath);
    free(outputFilePath);
    free(buffer);
}
