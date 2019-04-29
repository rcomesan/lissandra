#include "cx.h"
#include "fs.h"
#include "str.h"
#include "mem.h"
#include "math.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>

typedef struct stat stat_t;

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void _cx_fs_absolutize(cx_path_t* _inOutPath);

static bool _cx_fs_mkdir(const cx_path_t* _folderPath, cx_error_t* _err);

static bool _cx_fs_rmdir(const cx_path_t* _folderPath, cx_error_t* _err);

static bool _cx_fs_rm(const cx_path_t* _filePath, cx_error_t* _err);

 /****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void cx_fs_path(cx_path_t* _outPath, const char* _format, ...)
{
    va_list args;
    va_start(args, _format);
    vsnprintf(*_outPath, sizeof(*_outPath), _format, args);
    va_end(args);
    _cx_fs_absolutize(_outPath);

    //TODO. improvement: pre-process "." and ".." directories

    //TODO. improvement: get rid of multiple trailing slashes "///"
    // lastly, get rid of trailing slashes
    int32_t len = strlen(*_outPath);
    if (len > 1 && '/' == (*_outPath)[len - 1])
        (*_outPath)[len - 1] = '\0';
}

bool cx_fs_exists(const cx_path_t* _path)
{
    stat_t statBuf;
    return 0 == stat(*_path, &statBuf);
}

bool cx_fs_is_folder(const cx_path_t* _path)
{
    stat_t statBuf;
    if (0 == stat(*_path, &statBuf))
    {
        return S_ISDIR(statBuf.st_mode);
    }
    else
    {
        CX_WARN(CX_ALW, "path '%s' does not exist. you might want to call cx_fs_exists before calling this function.", *_path);
        return false;
    }
}

uint32_t cx_fs_get_size(const cx_path_t* _path)
{
    stat_t statBuf;
    if (0 == stat(*_path, &statBuf))
    {
        return statBuf.st_size;
    }
    else
    {
        CX_WARN(CX_ALW, "path '%s' does not exist. you might want to call cx_fs_exists before calling this function.", *_path);
        return false;
    }
}

void cx_fs_get_name(const cx_path_t* _path, bool _stripExtension, cx_path_t* _outName)
{
    char* slash = strrchr(*_path, '/');
    
    if (NULL == slash)
    {
        cx_str_copy(*_outName, sizeof(*_outName), *_path);
    }
    else if ((*_path) == slash)
    {
        (*_outName)[0] = '\0';
    }
    else
    {
        cx_str_copy(*_outName, sizeof(*_outName), slash + 1);
    }

    if (_stripExtension)
    {
        char* extension = strrchr(*_outName, '.');
        if (NULL != extension)
        {
            extension[0] = '\0';
        }
    }
}

void cx_fs_get_path(const cx_path_t* _path, cx_path_t* _outPath)
{
    char* slash = strrchr(*_path, '/');

    if (NULL == slash || (*_path) == slash)
    {
        (*_outPath)[0] = '\0';
    }
    else
    {
        cx_str_copy(*_outPath, sizeof(*_outPath), *_path);
        (*_outPath)[slash - (*_path)] = '\0';
    }
}

bool cx_fs_touch(const cx_path_t* _filePath, cx_error_t* _err)
{
    CX_CHECK(strlen(*_filePath) > 0, "invalid file path!");
    CX_ERROR_CLEAR(_err);

    if (!cx_fs_exists(_filePath))
    {
        cx_path_t parentFolderPath;
        cx_fs_get_path(_filePath, &parentFolderPath);

        if (cx_fs_mkdir(&parentFolderPath, _err))
        {
            // we'll stick to default privileges (664)
            int32_t fd = open(*_filePath, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);

            if (INVALID_DESCRIPTOR == fd)
            {
                CX_ERROR_SET(_err, 1, "file '%s' creation failed.", *_filePath);
            }
        }
        else
        {
            return false;
        }
    }
    return true;
}

bool cx_fs_mkdir(const cx_path_t* _folderPath, cx_error_t* _err)
{
    CX_CHECK(strlen(*_folderPath) > 0, "invalid folder path!");
    CX_ERROR_CLEAR(_err);
    
    if (cx_fs_exists(_folderPath) && cx_fs_is_folder(_folderPath)) return true;

    for (char* p = (*_folderPath) + 1; *p; p++)
    {
        if ('/' == *p)
        {
            *p = '\0';
            if (!_cx_fs_mkdir(_folderPath, _err)) return false;
            *p = '/';
        }
    }
    return _cx_fs_mkdir(_folderPath, _err);
}

bool cx_fs_remove(const cx_path_t* _path, cx_error_t* _err)
{
    CX_CHECK(strlen(*_path) > 0, "invalid folder path!");
    CX_ERROR_CLEAR(_err);
    
    if (cx_fs_exists(_path))
    {
        if (cx_fs_is_folder(_path))
        {
            _cx_fs_rmdir(_path, _err);
        }
        else
        {
            _cx_fs_rm(_path, _err);
        }
    }
    return CX_ERROR_OK(_err);
}

bool cx_fs_move(const cx_path_t* _path, const cx_path_t* _newPath, cx_error_t* _err)
{
    CX_CHECK(strlen(*_path) > 0, "invalid _path!");
    CX_CHECK(strlen(*_newPath) > 0, "invalid _newPath!");

    if (cx_fs_exists(_path))
    {
        if (!cx_fs_exists(_newPath))
        {
            cx_path_t destPath;
            cx_fs_get_path(_newPath, &destPath);

            if (cx_fs_exists(&destPath))
            {
                errno = 0;
                if (-1 == rename(*_path, *_newPath))
                {
                    CX_ERROR_SET(_err, 1, "Move failed. %s.", strerror(errno));
                }
            }
            else
            {
                CX_ERROR_SET(_err, 1, "Path '%s' could not be created.", *_newPath);
            }
        }
        else
        {
            CX_ERROR_SET(_err, 1, "File '%s' already exists.", *_newPath);
        }
    }
    else
    {
        CX_ERROR_SET(_err, 1, "File '%s' does not exist.", *_path);
    }
    return CX_ERROR_OK(_err);
}

bool cx_fs_write(const cx_path_t* _path, const char* _buffer, uint32_t _bufferSize, cx_error_t* _err)
{
    CX_CHECK(strlen(*_path) > 0, "invalid file path!");
    CX_ERROR_CLEAR(_err);

    if (cx_fs_touch(_path, _err))
    {
        FILE* fileHandle = fopen(*_path, "w");
        if (NULL != fileHandle)
        {
            if (NULL != _buffer)
            {
                CX_CHECK(_bufferSize > 0, "_bufferSize must be greater than zero!");

                if (_bufferSize > fwrite(_buffer, sizeof(char), _bufferSize, fileHandle))
                {
                    CX_ERROR_SET(_err, 1, "file '%s' could not be written. %s", *_path, strerror(errno));
                }
            }
            fflush(fileHandle);
            fclose(fileHandle);

            if (0 == _err->code) return true;
        }
        else
        {
            CX_ERROR_SET(_err, 1, "file '%s' could not be opened for writing.", *_path);
        }
    }

    return false;
}

int32_t cx_fs_read(const cx_path_t* _path, char* _outBuffer, uint32_t _bufferSize, cx_error_t* _err)
{
    CX_CHECK(strlen(*_path) > 0, "invalid file path!");
    CX_ERROR_CLEAR(_err);

    if (cx_fs_exists(_path))
    {
        if (!cx_fs_is_folder(_path))
        {
            uint32_t size = cx_fs_get_size(_path);

            if (NULL == _outBuffer)
            {
                return size;
            }
            else
            {
                CX_CHECK(_bufferSize > 0, "_bufferSize must be greater than zero!");
                
                FILE* fileHandle = fopen(*_path, "r");
                if (NULL != fileHandle)
                {
                    uint32_t bytesToRead = cx_math_min(size, _bufferSize);

                    if (bytesToRead > fread(_outBuffer, sizeof(char), bytesToRead, fileHandle))
                    {
                        CX_ERROR_SET(_err, 1, "file '%s' could not be read. %s", *_path, strerror(errno));
                    }
                    fclose(fileHandle);

                    if (0 == _err->code) return bytesToRead;
                }
                else
                {
                    CX_ERROR_SET(_err, 1, "file '%s' could not be opened for reading.", *_path);
                }
            }
        }
        else
        {
            CX_ERROR_SET(_err, 1, "path '%s' is a folder!", *_path);
        }
    }
    else
    {
        CX_ERROR_SET(_err, 1, "file '%s' is missing or not readable.", *_path);
    }

    return -1;
}

cx_fs_explorer_t* cx_fs_explorer_init(const cx_path_t* _folderPath, cx_error_t* _err)
{
    CX_CHECK(strlen(*_folderPath) > 0, "invalid folder path!");
    CX_ERROR_CLEAR(_err);

    DIR* dir = NULL;
    cx_fs_explorer_t* explorer = NULL;
    
    dir = opendir(*_folderPath);

    if (NULL != dir)
    {
        explorer = CX_MEM_STRUCT_ALLOC(explorer);
        cx_str_copy(explorer->path, sizeof(explorer->path), *_folderPath);
        explorer->dir = dir;
    }
    else
    {
        CX_ERROR_SET(_err, 1, "failed to open folder '%s'.", *_folderPath);
    }

    return explorer;
}

void cx_fs_explorer_reset(cx_fs_explorer_t* _explorer)
{
    CX_CHECK_NOT_NULL(_explorer);
    rewinddir(_explorer->dir);
}

bool cx_fs_explorer_next_file(cx_fs_explorer_t* _explorer, cx_path_t* _outFile)
{
    CX_CHECK_NOT_NULL(_explorer);

    if (!_explorer->readingFiles)
    {
        cx_fs_explorer_reset(_explorer);
        _explorer->readingFiles = true;
    }

    int32_t folderPathLen = strlen(_explorer->path);
    if (folderPathLen == 1 && _explorer->path[0] == '/') folderPathLen = 0;
    cx_str_copy(*_outFile, sizeof(*_outFile), _explorer->path);
    (*_outFile)[folderPathLen] = '/';
    (*_outFile)[folderPathLen + 1] = '\0';

    struct dirent* entry = readdir(_explorer->dir);
    while (NULL != entry)
    {
        cx_str_copy(&((*_outFile)[folderPathLen + 1]), sizeof(*_outFile) - (folderPathLen + 1), entry->d_name);

        if (!cx_fs_is_folder(_outFile))
        {
            return true;
        }
        entry = readdir(_explorer->dir);
    }
    return false;
}

bool cx_fs_explorer_next_folder(cx_fs_explorer_t* _explorer, cx_path_t* _outFolder)
{
    CX_CHECK_NOT_NULL(_explorer);

    if (_explorer->readingFiles)
    {
        cx_fs_explorer_reset(_explorer);
        _explorer->readingFiles = false;
    }

    int32_t folderPathLen = strlen(_explorer->path);
    if (folderPathLen == 1 && _explorer->path[0] == '/') folderPathLen = 0;
    cx_str_copy(*_outFolder, sizeof(*_outFolder), _explorer->path);
    (*_outFolder)[folderPathLen] = '/';
    (*_outFolder)[folderPathLen + 1] = '\0';

    struct dirent* entry = readdir(_explorer->dir);
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
        entry = readdir(_explorer->dir);
    }
    return false;
}

void cx_fs_explorer_destroy(cx_fs_explorer_t* _explorer)
{
    if (NULL == _explorer) return;
   
    if (NULL != _explorer->dir)
    {
        closedir(_explorer->dir);
        _explorer->dir = NULL;
    }

    free(_explorer);
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
        char* result = getcwd(absPath, sizeof(absPath));

        if (absPath == result)
        {
            int32_t absPathLen = strlen(absPath) + 1;
            absPath[absPathLen - 1] = '/';
            cx_str_copy(&absPath[absPathLen], sizeof(absPath) - absPathLen, *_inOutPath);
            cx_str_copy(*_inOutPath, sizeof(*_inOutPath), absPath);
        }
        CX_CHECK(absPath == result, "getcwd failed!");
    }
}

static bool _cx_fs_mkdir(const cx_path_t* _folderPath, cx_error_t* _err)
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
            CX_ERROR_SET(_err, 1, "a file already exists with the given path '%s'.", *_folderPath);
            return false;
        }
    }

    int32_t result = mkdir(*_folderPath, 0700);

    if (0 != result)
    {
        CX_ERROR_SET(_err, 1, "you do not have permissions to create the folder '%s'.", *_folderPath)
        return false;
    }

    return true;
}

static bool _cx_fs_rm(const cx_path_t* _filePath, cx_error_t* _err)
{
    if (0 == remove(*_filePath))
        return true;

    CX_ERROR_SET(_err, 1, "not enough permissions to delete file '%s'.", *_filePath)
    return false;
}

static bool _cx_fs_rmdir(const cx_path_t* _folderPath, cx_error_t* _err)
{
    cx_path_t path;
    cx_fs_explorer_t* brw = cx_fs_explorer_init(_folderPath, _err);
    if (NULL == brw) return false;

    bool failed = false;
    
    while (cx_fs_explorer_next_file(brw, &path) && !failed)
    {
        _cx_fs_rm(&path, _err);
        failed = (0 != _err->code);
    }

    while (cx_fs_explorer_next_folder(brw, &path) && !failed)
    {
        _cx_fs_rmdir(&path, _err);
        failed = (0 != _err->code);
    }

    cx_fs_explorer_destroy(brw);

    return !failed && (0 == rmdir(*_folderPath));
}