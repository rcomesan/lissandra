#ifndef CX_FSWATCH_H_
#define CX_FSWATCH_H_

#include "cx.h"
#include "halloc.h"
#include "file.h"

#include <sys/epoll.h>
#include <sys/inotify.h>

typedef struct epoll_event epoll_event;
typedef struct inotify_event inotify_event;

#define EVENT_SIZE          (sizeof(inotify_event))
#define EVENT_BUF_LEN       (128 * (EVENT_SIZE + 16))

typedef void(*cx_fswatch_handler_cb)(const char* _path, uint32_t _mask, void* _userData);

typedef struct cx_fswatch_instance_t
{
    int32_t                 wd;                 // watch descriptor for this watched file or directory.
    cx_path_t               filePath;           // path to the watched file or directory.
    void*                   userData;           // pointer to user data which will be passed through to the given event handler when invoked.
} cx_fswatch_instance_t;

typedef struct cx_fswatch_ctx_t
{
    uint16_t                maxFiles;           // maximum amount of files to watch.
    int32_t                 ifd;                // inotify watch descriptor.
    cx_fswatch_handler_cb   handler;            // callback to the function that will handle filesystem events.
    char                    buff[EVENT_BUF_LEN];// buffer to fill with inotify events.
    int32_t                 epollDescriptor;    // file descriptor to the epoll instance.
    cx_fswatch_instance_t*  files;              // container for storing watched files or directories.
    cx_handle_alloc_t*      filesHalloc;        // handle allocator for files container.
} cx_fswatch_ctx_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool            cx_fswatch_init(uint16_t _maxFiles, cx_fswatch_handler_cb _handler, cx_err_t* _err);

void            cx_fswatch_destroy();

uint16_t        cx_fswatch_count();

uint16_t        cx_fswatch_add(const char* _path, uint32_t _mask, void* _userData);

void            cx_fswatch_remove(uint16_t _fswatchHandle);

void            cx_fswatch_poll_events();

#endif // CX_FSWATCH_H_