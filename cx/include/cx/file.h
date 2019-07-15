#ifndef cx_file_H_
#define cx_file_H_

#include "cx.h"

#include <stdbool.h>
#include <limits.h>
#include <dirent.h>

typedef char cx_path_t[PATH_MAX + 1];

typedef struct cx_file_explorer_t
{
    cx_path_t   path;
    DIR*        dir;
    bool        readingFiles;
} cx_file_explorer_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void                cx_file_path(cx_path_t* _outPath, const char* _format, ...);

bool                cx_file_exists(const cx_path_t* _path);

bool                cx_file_is_folder(const cx_path_t* _path);

uint32_t            cx_file_get_size(const cx_path_t* _path);

void                cx_file_get_name(const cx_path_t* _path, bool _stripExtension, cx_path_t* _outName);

void                cx_file_get_path(const cx_path_t* _path, cx_path_t* _outPath);

void                cx_file_set_extension(const cx_path_t* _path, const char* _extension, cx_path_t* _outPath);

bool                cx_file_touch(const cx_path_t* _filePath, cx_err_t* _err);

bool                cx_file_mkdir(const cx_path_t* _folderPath, cx_err_t* _err);

bool                cx_file_remove(const cx_path_t* _path, cx_err_t* _err);

bool                cx_file_move(const cx_path_t* _path, const cx_path_t* _newPath, cx_err_t* _err);

bool                cx_file_write(const cx_path_t* _path, const char* _buffer, uint32_t _bufferSize, cx_err_t* _err);

int32_t             cx_file_read(const cx_path_t* _path, char* _outBuffer, uint32_t _bufferSize, cx_err_t* _err);

cx_file_explorer_t* cx_file_explorer_init(const cx_path_t* _folderPath, cx_err_t* _err);

void                cx_file_explorer_reset(cx_file_explorer_t* _explorer);

bool                cx_file_explorer_next_file(cx_file_explorer_t* _explorer, cx_path_t* _outFile);

bool                cx_file_explorer_next_folder(cx_file_explorer_t* _explorer, cx_path_t* _outFolder);

void                cx_file_explorer_destroy(cx_file_explorer_t* _explorer);

#endif
