#ifndef CX_FS_H_
#define CX_FS_H_

#include "cx.h"

#include <stdbool.h>
#include <limits.h>
#include <dirent.h>

typedef char cx_path_t[PATH_MAX + 1];

typedef struct cx_fs_browser_t
{
    cx_path_t   path;
    DIR*        dir;
    bool        readingFiles;
} cx_fs_browser_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void                cx_fs_path(cx_path_t* _path, const char* _format, ...);

bool                cx_fs_exists(const cx_path_t* _path);

bool                cx_fs_is_folder(const cx_path_t* _path);

bool                cx_fs_mkdir(const cx_path_t* _folderPath, cx_error_t* _outErr);

bool                cx_fs_remove(const cx_path_t* _path, cx_error_t* _outErr);

cx_fs_browser_t*    cx_fs_browser_init(const cx_path_t* _folderPath, cx_error_t* _outErr);

bool                cx_fs_browser_next_file(cx_fs_browser_t* _browser, cx_path_t* _outFile);

bool                cx_fs_browser_next_folder(cx_fs_browser_t* _browser, cx_path_t* _outFolder);

void                cx_fs_browser_terminate(cx_fs_browser_t* _browser);

#endif
