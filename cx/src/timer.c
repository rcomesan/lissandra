#include "cx.h"
#include "timer.h"
#include "mem.h"

#include <stdio.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static cx_timer_ctx_t       m_timerCtx;        // private timer context

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool                 _cx_timer_epoll_mod(int32_t _fd, bool _set);

static void                 _cx_timer_free(cx_timer_instance_t* _timer);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool cx_timer_init(uint16_t _maxTimers, cx_timer_handler_cb _handler, cx_err_t* _err)
{
    CX_MEM_ZERO(m_timerCtx);
    bool success = false;

    CX_INFO("initializing timer module...");

    struct timeval now;
    gettimeofday(&now, 0);
    m_timerCtx.timeOffset = now.tv_sec * INT64_C(1000000) + now.tv_usec;

    if (_maxTimers > 0)
    {
        m_timerCtx.maxTimers = _maxTimers;
        m_timerCtx.handler = _handler;

        m_timerCtx.epollDescriptor = epoll_create(_maxTimers);
        if (INVALID_DESCRIPTOR != m_timerCtx.epollDescriptor)
        {
            m_timerCtx.epollEvents = CX_MEM_ARR_ALLOC(m_timerCtx.epollEvents, _maxTimers);
            m_timerCtx.timers = CX_MEM_ARR_ALLOC(m_timerCtx.timers, _maxTimers);
            m_timerCtx.timersHalloc = cx_halloc_init(_maxTimers);
            if (NULL != m_timerCtx.timersHalloc)
            {
                CX_INFO("timer module initialization succeeded.");
                success = true;
            }
            else
            {
                CX_ERR_SET(_err, 1, "timers handle allocator creation failed.");
            }
        }
        else
        {
            CX_ERR_SET(_err, 1, "timers epoll_create failed. %s.", strerror(errno));
        }
    }
    else
    {
        m_timerCtx.epollDescriptor = INVALID_DESCRIPTOR;
        m_timerCtx.epollEvents = NULL;
        m_timerCtx.timers = NULL;
        m_timerCtx.timersHalloc = NULL;
        success = true;
    }

    if (!success) cx_timer_destroy();
    return success;
}

void cx_timer_destroy()
{
    CX_INFO("terminating timer module...");

    uint16_t max = cx_handle_count(m_timerCtx.timersHalloc);
    uint16_t handle = INVALID_HANDLE;
    cx_timer_instance_t* timer = NULL;

    for (uint16_t i = 0; i < max; i++)
    {
        handle = cx_handle_at(m_timerCtx.timersHalloc, i);
        timer = &(m_timerCtx.timers[handle]);

        _cx_timer_free(timer);
    }

    if (NULL != m_timerCtx.timersHalloc)
    {
        cx_halloc_destroy(m_timerCtx.timersHalloc);
        m_timerCtx.timersHalloc = NULL;
    }

    if (INVALID_DESCRIPTOR != m_timerCtx.epollDescriptor)
    {
        close(m_timerCtx.epollDescriptor);
        m_timerCtx.epollDescriptor = INVALID_DESCRIPTOR;
    }

    if (NULL != m_timerCtx.epollEvents)
    {
        free(m_timerCtx.epollEvents);
        m_timerCtx.epollEvents = NULL;
    }

    if (NULL != m_timerCtx.timers)
    {
        free(m_timerCtx.timers);
        m_timerCtx.timers = NULL;
    }
    
    CX_INFO("timer module terminated gracefully.");
}

uint16_t cx_timer_count()
{
    return cx_handle_count(m_timerCtx.timersHalloc);
}

uint16_t cx_timer_add(uint32_t _interval, uint32_t _id, void* _userData)
{
    bool success = false;
    uint16_t timerHandle = INVALID_HANDLE;
    cx_timer_instance_t* timer = NULL;

    if (cx_handle_count(m_timerCtx.timersHalloc) == m_timerCtx.maxTimers)
    {
        CX_CHECK(CX_ALW, "timers container is full!");
        return INVALID_HANDLE;
    }

    int32_t fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (INVALID_DESCRIPTOR != fd)
    {
        struct itimerspec ts;
        CX_MEM_ZERO(ts);
        ts.it_value.tv_sec = _interval / 1000;
        ts.it_value.tv_nsec = (_interval % 1000) * 1000000;
        ts.it_interval.tv_sec = ts.it_value.tv_sec;
        ts.it_interval.tv_nsec = ts.it_value.tv_nsec;

        if (0 == timerfd_settime(fd, 0, &ts, NULL))
        {
            if (_cx_timer_epoll_mod(fd, true))
            {
                timerHandle = cx_handle_alloc_key(m_timerCtx.timersHalloc, fd);
                if (INVALID_HANDLE != timerHandle)
                {
                    timer = &m_timerCtx.timers[timerHandle];
                    timer->fd = fd;
                    timer->id = _id;
                    timer->userData = _userData;
                    success = true;
                }
                CX_CHECK(INVALID_HANDLE != timerHandle, "timer handle allocation failed!");
            }
        }
        else
        {
            CX_WARN(CX_ALW, "timers timerfd_settime failed. %s.", strerror(errno));
        }
    }
    else
    {
        CX_WARN(CX_ALW, "timers timerfd_create failed. %s.", strerror(errno));
    }

    if (!success && INVALID_DESCRIPTOR != fd) close(fd);
    return timerHandle;
}

void cx_timer_remove(uint16_t _timerHandle)
{
    if (!cx_handle_is_valid(m_timerCtx.timersHalloc, _timerHandle))
        return;
    
    cx_timer_instance_t* timer = &m_timerCtx.timers[_timerHandle];
    
    if (INVALID_DESCRIPTOR != timer->fd)
    {
        _cx_timer_epoll_mod(timer->fd, false);
        _cx_timer_free(timer);
    }

    cx_handle_free(m_timerCtx.timersHalloc, _timerHandle);
}

void cx_timer_modify(uint16_t _timerHandle, uint32_t _newInterval)
{
    if (!cx_handle_is_valid(m_timerCtx.timersHalloc, _timerHandle))
        return;

    cx_timer_instance_t* timer = &m_timerCtx.timers[_timerHandle];
    
    struct itimerspec ts;
    CX_MEM_ZERO(ts);
    ts.it_interval.tv_sec = _newInterval / 1000;
    ts.it_interval.tv_nsec = (_newInterval % 1000) * 1000000;

    bool success = (0 == timerfd_settime(timer->fd, 0, &ts, NULL));
    CX_CHECK(success, "timer handle %d modification failed!", _timerHandle);
}

void cx_timer_poll_events()
{
    int32_t fd = INVALID_DESCRIPTOR;
    uint16_t timerHandle = INVALID_HANDLE;
    cx_timer_instance_t* timer = NULL;
    uint64_t expirations = 0;
    int32_t bytesRead = 0;

    int32_t eventsCount = epoll_wait(m_timerCtx.epollDescriptor, m_timerCtx.epollEvents, m_timerCtx.maxTimers, 0);
    CX_WARN(-1 != eventsCount, "timer epoll_wait failed. %s.", strerror(errno));

    // handle epoll events
    for (int32_t i = 0; i < eventsCount; i++)
    {
        fd = m_timerCtx.epollEvents[i].data.fd;
        timerHandle = cx_handle_get(m_timerCtx.timersHalloc, fd);
        if (INVALID_HANDLE != timerHandle)
        {
            timer = &m_timerCtx.timers[timerHandle];
            bytesRead = read(timer->fd, &expirations, sizeof(expirations));

            if (sizeof(expirations) == bytesRead)
            {
                if (!m_timerCtx.handler(expirations, timer->id, timer->userData))
                {
                    cx_timer_remove(timerHandle);
                }
            }
            CX_WARN(sizeof(expirations) == bytesRead, "timer read didn't return the expected amount of bytes!");
        }
        CX_CHECK(INVALID_HANDLE != timerHandle, "something's wrong with timer module event polling!");
    }
}

static bool _cx_timer_epoll_mod(int32_t _fd, bool _set)
{
    bool success = true;

    if (_set)
    {
        epoll_event event;
        CX_MEM_ZERO(event);
        event.events = EPOLLIN;
        event.data.fd = _fd;
        success = (0 == epoll_ctl(m_timerCtx.epollDescriptor, EPOLL_CTL_ADD, _fd, &event));
        CX_WARN(success, "timers epoll_ctl add failed. %s.", strerror(errno));
    }
    else
    {
        // unset
        success = (0 == epoll_ctl(m_timerCtx.epollDescriptor, EPOLL_CTL_DEL, _fd, NULL));
        CX_WARN(success, "timers epoll_ctl del failed. %s.", strerror(errno));
    }

    return success;
}

double cx_time_counter()
{
    struct timeval now;
    gettimeofday(&now, 0);

    int64_t time = now.tv_sec * INT64_C(1000000) + now.tv_usec;
    return (double)(time - m_timerCtx.timeOffset) / (double)INT64_C(1000000);
}

uint32_t cx_time_epoch()
{
    //TODO CHECKME is seconds precision OK for this?
    return time(NULL);
}

void cx_time_stamp(cx_timestamp_t* _outTimestamp)
{
    time_t now = time(NULL);
    strftime(*_outTimestamp, 15, "%Y%m%d%H%M%S", localtime(&now));
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static void _cx_timer_free(cx_timer_instance_t* _timer)
{
    close(_timer->fd);
    CX_MEM_ZERO(*_timer);
}