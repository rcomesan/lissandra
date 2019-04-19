#ifndef CX_FS_H_
#define CX_FS_H_

#include "cx.h"

#include <stdbool.h>
#include <limits.h>
#include <dirent.h>

typedef char cx_path_t[PATH_MAX + 1];

typedef struct cx_fs_explorer_t
{
    cx_path_t   path;
    DIR*        dir;
    bool        readingFiles;
} cx_fs_explorer_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void                cx_fs_path(cx_path_t* _outPath, const char* _format, ...);

bool                cx_fs_exists(const cx_path_t* _path);

bool                cx_fs_is_folder(const cx_path_t* _path);

uint32_t            cx_fs_get_size(const cx_path_t* _path);

void                cx_fs_get_name(const cx_path_t* _path, bool _stripExtension, cx_path_t* _outName);

void                cx_fs_get_path(const cx_path_t* _path, cx_path_t* _outPath);

bool                cx_fs_touch(const cx_path_t* _filePath, cx_error_t* _err);

bool                cx_fs_mkdir(const cx_path_t* _folderPath, cx_error_t* _err);

bool                cx_fs_remove(const cx_path_t* _path, cx_error_t* _err);

bool                cx_fs_write(const cx_path_t* _path, const char* _buffer, uint32_t _bufferSize, cx_error_t* _err);

uint32_t            cx_fs_read(const cx_path_t* _path, char* _outBuffer, uint32_t _bufferSize, cx_error_t* _err);

cx_fs_explorer_t*   cx_fs_explorer_init(const cx_path_t* _folderPath, cx_error_t* _err);

bool                cx_fs_explorer_next_file(cx_fs_explorer_t* _explorer, cx_path_t* _outFile);

bool                cx_fs_explorer_next_folder(cx_fs_explorer_t* _explorer, cx_path_t* _outFolder);

void                cx_fs_explorer_destroy(cx_fs_explorer_t* _explorer);

#endif
