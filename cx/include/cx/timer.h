#ifndef CX_TIMER_H_
#define CX_TIMER_H_

#include "cx.h"
#include "halloc.h"

#include <sys/epoll.h>
#include <stdint.h>
#include <stdbool.h>

typedef bool(*cx_timer_handler_cb)(uint64_t _expirations, uint32_t _id, void* _userData);

typedef struct epoll_event epoll_event;

typedef struct cx_timer_instance_t
{
    int32_t     fd;                             // file descriptor for this timer.
    uint32_t    id;                             // timer identifier which will be passed through to the given tick event andler when invoked.
    void*       userData;                       // pointer to user data which will be passed through to the given tick event handler when invoked.
} cx_timer_instance_t;

typedef struct cx_timer_ctx_t
{
    // Asynchronous timers variables.
    uint16_t                maxTimers;          // maximum amount of timers allowed.
    cx_timer_handler_cb     handler;            // callback to the function that will handle our timers tick event.
    int32_t                 epollDescriptor;    // file descriptor to the epoll instance.
    epoll_event*            epollEvents;        // pre-allocated buffer for retrieving epoll events when calling epoll_wait.
    cx_timer_instance_t*    timers;             // container for storing timers.
    cx_handle_alloc_t*      timersHalloc;       // handle allocator for timers container.
    
    // Generic timing functions variables.
    int64_t                 timeOffset;
} cx_timer_ctx_t;


/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

// Asynchronous timers.

bool            cx_timer_init(uint16_t _maxTimers, cx_timer_handler_cb _handler, cx_err_t* _err);

void            cx_timer_destroy();

uint16_t        cx_timer_count();

uint16_t        cx_timer_add(uint32_t _interval, uint32_t _id, void* _userData);

void            cx_timer_remove(uint16_t _timerHandle);

void            cx_timer_modify(uint16_t _timerHandle, uint32_t _newInterval);

void            cx_timer_poll_events();

// Generic timing functions.

double          cx_time_counter();

uint32_t        cx_time_epoch();

uint64_t        cx_time_epoch_ms();

void            cx_time_stamp(cx_timestamp_t* _outTimestamp);

void            cx_time_sleep(uint32_t _milliseconds);

#endif // CX_TIMER_H_
