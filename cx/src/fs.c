#include "cx.h"
#include "fs.h"
#include "str.h"
#include "mem.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct stat stat_t;

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void _cx_fs_absolutize(cx_path_t* _inOutPath);

static bool _cx_fs_mkdir(const cx_path_t* _folderPath, cx_error_t* _outErr);

static bool _cx_fs_rmdir(const cx_path_t* _folderPath, cx_error_t* _outErr);

static bool _cx_fs_rm(const cx_path_t* _filePath, cx_error_t* _outErr);

 /****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void cx_fs_path(cx_path_t* _path, const char* _format, ...)
{
    va_list args;
    va_start(args, _format);
    vsnprintf(_path, sizeof(*_path), _format, args);
    va_end(args);
    _cx_fs_absolutize(_path);
}

bool cx_fs_exists(const cx_path_t* _path)
{
    stat_t statBuf;
    return 0 == stat(_path, &statBuf);
}

bool cx_fs_is_folder(const cx_path_t* _path)
{
    stat_t statBuf;
    if (0 == stat(_path, &statBuf))
    {
        return S_ISDIR(statBuf.st_mode);
    }
    else
    {
        CX_WARN(CX_ALW, "Path '%s' does not exist. You might want to call cx_fs_exists before calling this function.", *_path);
        return false;
    }
}

bool cx_fs_mkdir(const cx_path_t* _folderPath, cx_error_t* _outErr)
{
    int32_t len = strlen(*_folderPath);

    CX_CHECK(len > 0, "invalid folder path!");
    CX_MEM_ZERO(*_outErr);   
    
    for (char* p = (*_folderPath) + 1; *p; p++)
    {
        if ('/' == *p)
        {
            *p = '\0';
            if (!_cx_fs_mkdir(_folderPath, _outErr)) return;
            *p = '/';
        }
    }
    return _cx_fs_mkdir(_folderPath, _outErr);
}

bool cx_fs_remove(const cx_path_t* _path, cx_error_t* _outErr)
{
    CX_CHECK(strlen(*_path) > 0, "invalid folder path!");
    CX_MEM_ZERO(*_outErr);
    
    if (cx_fs_exists(_path))
    {
        if (cx_fs_is_folder(_path))
        {
            _cx_fs_rmdir(_path, _outErr);
        }
        else
        {
            _cx_fs_rm(_path, _outErr);
        }
        return (0 != _outErr->num);
    }
    return true;
}

cx_fs_browser_t* cx_fs_browser_init(const cx_path_t* _folderPath, cx_error_t* _outErr)
{
    CX_CHECK(strlen(*_folderPath) > 0, "invalid folder path!");
    DIR* dir = NULL;
    cx_fs_browser_t* browser = NULL;
    
    dir = opendir(*_folderPath);

    if (NULL != dir)
    {
        browser = CX_MEM_STRUCT_ALLOC(browser);
        cx_str_copy(browser->path, sizeof(browser->path), _folderPath);
        browser->dir = dir;
    }
    else
    {
        CX_ERROR_SET(_outErr, 1, "Failed to open folder '%s'.", _folderPath);
    }

    return browser;
}

bool cx_fs_browser_next_file(cx_fs_browser_t* _browser, cx_path_t* _outFile)
{
    if (!_browser->readingFiles)
    {
        rewinddir(_browser->dir);
        _browser->readingFiles = true;
    }

    int32_t folderPathLen = strlen(_browser->path);
    if (folderPathLen == 1 && _browser->path[0] == '/') folderPathLen = 0;
    cx_str_copy(*_outFile, sizeof(*_outFile), _browser->path);
    (*_outFile)[folderPathLen] = '/';
    (*_outFile)[folderPathLen + 1] = '\0';

    struct dirent* entry = readdir(_browser->dir);
    while (NULL != entry)
    {
        cx_str_copy(&((*_outFile)[folderPathLen + 1]), sizeof(*_outFile) - (folderPathLen + 1), entry->d_name);

        if (!cx_fs_is_folder(_outFile))
        {
            return true;
        }
        entry = readdir(_browser->dir);
    }
    return false;
}

bool cx_fs_browser_next_folder(cx_fs_browser_t* _browser, cx_path_t* _outFolder)
{
    if (_browser->readingFiles)
    {
        rewinddir(_browser->dir);
        _browser->readingFiles = false;
    }

    int32_t folderPathLen = strlen(_browser->path);
    if (folderPathLen == 1 && _browser->path[0] == '/') folderPathLen = 0;
    cx_str_copy(*_outFolder, sizeof(*_outFolder), _browser->path);
    (*_outFolder)[folderPathLen] = '/';
    (*_outFolder)[folderPathLen + 1] = '\0';

    struct dirent* entry = readdir(_browser->dir);
    while (NULL != entry)
    {
        if (true
            && 0 != strncmp(entry->d_name, ".", 1)
            && 0 != strncmp(entry->d_name, "..", 2))
        {
            cx_str_copy(&((*_outFolder)[folderPathLen + 1]), sizeof(*_outFolder) - (folderPathLen + 1), entry->d_name);

            if (cx_fs_is_folder(_outFolder))
            {
                return true;
            }
        }
        entry = readdir(_browser->dir);
    }
    return false;
}

void cx_fs_browser_terminate(cx_fs_browser_t* _browser)
{
    if (NULL == _browser) return;
   
    if (NULL != _browser->dir)
    {
        closedir(_browser->dir);
        _browser->dir = NULL;
    }

    free(_browser);
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

void _cx_fs_absolutize(cx_path_t* _inOutPath)
{
    CX_CHECK(strlen(*_inOutPath) > 0, "invalid path!");

    if ('/' != (*_inOutPath)[0])
    {
        cx_path_t absPath;
        int32_t result = getcwd(&absPath, sizeof(absPath));

        if (NULL != result)
        {
            int32_t absPathLen = strlen(absPath) + 1;
            absPath[absPathLen - 1] = '/';
            cx_str_copy(&absPath[absPathLen], sizeof(absPath) - absPathLen, *_inOutPath);
            cx_str_copy(*_inOutPath, sizeof(*_inOutPath), absPath);
        }
        CX_CHECK(NULL != result, "getcwd failed!");
    }

    // lastly, get rid of trailing slashes
    int32_t len = strlen(*_inOutPath);
    if (len > 1 && '/' == (*_inOutPath)[len - 1])
        (*_inOutPath)[len - 1] = '\0';
}

static bool _cx_fs_mkdir(const cx_path_t* _folderPath, cx_error_t* _outErr)
{
    CX_CHECK(strlen(*_folderPath) > 0, "invalid folder path!");

    if (cx_fs_exists(_folderPath))
    {
        if (cx_fs_is_folder(_folderPath))
        {
            return true;
        }
        else
        {
            CX_ERROR_SET(_outErr, 1, "A file already exists with the given path '%s'.", *_folderPath);
            return false;
        }
    }

    int32_t result = mkdir(*_folderPath, 0700);

    if (0 != result)
    {
        CX_ERROR_SET(_outErr, 1, "You do not have permissions to create the folder '%s'.", *_folderPath)
        return false;
    }

    return true;
}

static bool _cx_fs_rm(const cx_path_t* _filePath, cx_error_t* _outErr)
{
    if (0 == remove(_filePath))
        return true;

    CX_ERROR_SET(_outErr, 1, "Not enough permissions to delete file '%s'.", *_filePath)
    return false;
}

static bool _cx_fs_rmdir(const cx_path_t* _folderPath, cx_error_t* _outErr)
{
    cx_path_t path;
    cx_fs_browser_t* brw = cx_fs_browser_init(_folderPath, _outErr);
    if (NULL == brw) return false;

    bool failed = false;
    
    while (cx_fs_browser_next_file(brw, &path) && !failed)
    {
        _cx_fs_rm(path, _outErr);
        failed = (0 != _outErr->num);
    }

    while (cx_fs_browser_next_folder(brw, &path) && !failed)
    {
        _cx_fs_rmdir(path, _outErr);
        failed = (0 != _outErr->num);
    }

    cx_fs_browser_terminate(brw);

    return !failed && (0 == rmdir(*_folderPath));
}