#include "cx.h"
#include "mem.h"
#include "fswatch.h"
#include "str.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>

static cx_fswatch_ctx_t       m_fswatchCtx;        // private fswatch context

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void                 _cx_fswatch_free(cx_fswatch_instance_t* _fswatch);

/****************************************************************************************
***  PUBLIC FUNCTIONS
***************************************************************************************/

bool cx_fswatch_init(uint16_t _maxFiles, cx_fswatch_handler_cb _handler, cx_err_t* _err)
{
    CX_MEM_ZERO(m_fswatchCtx);
    bool success = false;

    CX_INFO("initializing fswatch module...");

    if (_maxFiles > 0)
    {
        m_fswatchCtx.maxFiles = _maxFiles;
        m_fswatchCtx.handler = _handler;

        m_fswatchCtx.ifd = inotify_init();
        if (INVALID_DESCRIPTOR != m_fswatchCtx.ifd)
        {
            m_fswatchCtx.epollDescriptor = epoll_create(1);
            if (INVALID_DESCRIPTOR != m_fswatchCtx.epollDescriptor)
            {
                epoll_event event;
                CX_MEM_ZERO(event);
                event.events = EPOLLIN;
                event.data.fd = m_fswatchCtx.ifd;
                if (0 ==epoll_ctl(m_fswatchCtx.epollDescriptor, EPOLL_CTL_ADD, m_fswatchCtx.ifd, &event))
                {
                    m_fswatchCtx.files = CX_MEM_ARR_ALLOC(m_fswatchCtx.files, _maxFiles);
                    m_fswatchCtx.filesHalloc = cx_halloc_init(_maxFiles);
                    if (NULL != m_fswatchCtx.filesHalloc)
                    {
                        CX_INFO("fswatch module initialization succeeded.");
                        success = true;
                    }
                    else
                    {
                        CX_ERR_SET(_err, 1, "fswatch handle allocator creation failed.");
                    }
                }
                else
                {
                    CX_ERR_SET(_err, 1, "fswatch epoll_ctl add failed. %s.", strerror(errno));
                }
            }
            else
            {
                CX_ERR_SET(_err, 1, "fswatch epoll_create failed. %s.", strerror(errno));
            }
        }
        else
        {
            CX_ERR_SET(_err, 1, "fswatch inotify_init failed. %s.", strerror(errno));
        }
    }
    else
    {
        m_fswatchCtx.files = NULL;
        m_fswatchCtx.filesHalloc = NULL;
        success = true;
    }

    if (!success) cx_fswatch_destroy();
    return success;
}

void cx_fswatch_destroy()
{
    CX_INFO("terminating fswatch module...");

    if (NULL != m_fswatchCtx.filesHalloc)
    {
        uint16_t max = cx_handle_count(m_fswatchCtx.filesHalloc);
        uint16_t handle = INVALID_HANDLE;
        cx_fswatch_instance_t* file = NULL;

        for (uint16_t i = 0; i < max; i++)
        {
            handle = cx_handle_at(m_fswatchCtx.filesHalloc, i);
            file = &(m_fswatchCtx.files[handle]);

            _cx_fswatch_free(file);
        }

        cx_halloc_destroy(m_fswatchCtx.filesHalloc);
        m_fswatchCtx.filesHalloc = NULL;
    }

    if (INVALID_DESCRIPTOR != m_fswatchCtx.epollDescriptor)
    {
        close(m_fswatchCtx.epollDescriptor);
        m_fswatchCtx.epollDescriptor = INVALID_DESCRIPTOR;
    }

    if (NULL != m_fswatchCtx.files)
    {
        free(m_fswatchCtx.files);
        m_fswatchCtx.files = NULL;
    }

    if (INVALID_DESCRIPTOR != m_fswatchCtx.ifd)
    {
        close(m_fswatchCtx.ifd);
        m_fswatchCtx.ifd = INVALID_DESCRIPTOR;
    }

    CX_INFO("fswatch module terminated gracefully.");
}

uint16_t cx_fswatch_count()
{
    return cx_handle_count(m_fswatchCtx.filesHalloc);
}

uint16_t cx_fswatch_add(const char* _path, uint32_t _mask, void* _userData)
{
    bool success = false;
    uint16_t fswatchHandle = INVALID_HANDLE;
    cx_fswatch_instance_t* file = NULL;

    if (cx_handle_count(m_fswatchCtx.filesHalloc) == m_fswatchCtx.maxFiles)
    {
        CX_CHECK(CX_ALW, "files container is full!");
        return INVALID_HANDLE;
    }

    int32_t wd = inotify_add_watch(m_fswatchCtx.ifd, _path, _mask);
    if (INVALID_DESCRIPTOR != wd)
    {
        fswatchHandle = cx_handle_alloc_key(m_fswatchCtx.filesHalloc, wd);
        if (INVALID_HANDLE != fswatchHandle)
        {
            file = &m_fswatchCtx.files[fswatchHandle];
            file->wd = wd;
            file->userData = _userData;
            cx_str_copy(file->filePath, sizeof(file->filePath), _path);
            success = true;
        }
        CX_CHECK(INVALID_HANDLE != fswatchHandle, "fswatch handle allocation failed!");
    }
    else
    {
        CX_WARN(CX_ALW, "fswatch inotify_add_watch failed. %s.", strerror(errno));
    }

    if (!success && INVALID_DESCRIPTOR != wd) close(wd);
    return fswatchHandle;
}

void cx_fswatch_remove(uint16_t _fswatchHandle)
{
    if (!cx_handle_is_valid(m_fswatchCtx.filesHalloc, _fswatchHandle))
        return;

    cx_fswatch_instance_t* file = &m_fswatchCtx.files[_fswatchHandle];

    if (INVALID_DESCRIPTOR != file->wd)
    {
        _cx_fswatch_free(file);
    }

    cx_handle_free(m_fswatchCtx.filesHalloc, _fswatchHandle);
}

void cx_fswatch_poll_events()
{
    uint16_t fileHandle = INVALID_HANDLE;
    cx_fswatch_instance_t* file = NULL;
    epoll_event epollEvent;
    inotify_event* event = NULL;
    int32_t pos = 0;
    int32_t bytesRead = 0;
    char* endOfPath = NULL;

    int32_t eventsCount = epoll_wait(m_fswatchCtx.epollDescriptor, &epollEvent, 1, 0);
    CX_WARN(-1 != eventsCount, "fswatch epoll_wait failed. %s.", strerror(errno));

    // handle epoll events
    if (eventsCount == 1)
    {
        bytesRead = read(m_fswatchCtx.ifd, m_fswatchCtx.buff, sizeof(m_fswatchCtx.buff));
        pos = 0;

        while (pos < bytesRead)
        {
            event = (inotify_event*)&m_fswatchCtx.buff[pos];

            fileHandle = cx_handle_get(m_fswatchCtx.filesHalloc, event->wd);
            if (INVALID_HANDLE != fileHandle)
            {
                file = &m_fswatchCtx.files[fileHandle];

                if (event->len > 0)
                {
                    // store end of the original file path
                    endOfPath = &file->filePath[strlen(file->filePath)];

                    // append the file
                    cx_str_cat(file->filePath, sizeof(file->filePath), "/");
                    cx_str_cat(file->filePath, sizeof(file->filePath), event->name);
                }

                m_fswatchCtx.handler(file->filePath, event->mask, file->userData);

                // restore original string
                if (event->len > 0)
                    (*endOfPath) = '\0';
            }
            CX_CHECK(INVALID_HANDLE != fileHandle, "something's wrong with fswatch module event polling!");

            pos += EVENT_SIZE + event->len;
        }
        CX_WARN(0 < bytesRead, "fswatch read failed!");
    }
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static void _cx_fswatch_free(cx_fswatch_instance_t* _file)
{
    inotify_rm_watch(m_fswatchCtx.ifd, _file->wd);
    CX_MEM_ZERO(*_file);
}