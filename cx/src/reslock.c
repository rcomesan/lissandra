#include "reslock.h"
#include "mem.h"
#include "timer.h"

bool cx_reslock_init(cx_reslock_t* _lock, bool _startsBlocked)
{
    CX_MEM_ZERO(*_lock);
    if (0 == pthread_mutex_init(&_lock->mtx, NULL))
    {
        if (_startsBlocked)
            cx_reslock_block(_lock);

        return true;
    }
    return false;
}

void cx_reslock_destroy(cx_reslock_t* _lock)
{
    pthread_mutex_destroy(&_lock->mtx);
}

bool cx_reslock_avail_guard_begin(cx_reslock_t* _lock)
{
    bool result = false;

    pthread_mutex_lock(&_lock->mtx);
    if (_lock->blocked)
    {
        result = false;
    }
    else
    {
        // increment the amount of running jobs that depend on this resource being available
        _lock->counter++;
        result = true;
    }    
    pthread_mutex_unlock(&_lock->mtx);

    return result;
}

void cx_reslock_avail_guard_end(cx_reslock_t* _lock)
{
    pthread_mutex_lock(&_lock->mtx);
    CX_CHECK(_lock->counter > 0, "avail_guard begin/end mismatch!")
    _lock->counter--;
    pthread_mutex_unlock(&_lock->mtx);
}

bool cx_reslock_is_blocked(cx_reslock_t* _lock)
{
    bool isBlocked = false;
    pthread_mutex_lock(&_lock->mtx);
    isBlocked = _lock->blocked;
    pthread_mutex_unlock(&_lock->mtx);
    return isBlocked;
}

void cx_reslock_block(cx_reslock_t* _lock)
{
    pthread_mutex_lock(&_lock->mtx);
    CX_CHECK(!_lock->blocked, "the resource protected is already blocked!");
    _lock->blocked = true;
    _lock->blockedStartTime = cx_time_counter();
    pthread_mutex_unlock(&_lock->mtx);
}

void cx_reslock_unblock(cx_reslock_t* _lock)
{
    pthread_mutex_lock(&_lock->mtx);
    CX_CHECK(_lock->blocked, "the resource protected is not blocked!");
    _lock->blocked = false;
    _lock->blockedTime = cx_time_counter() - _lock->blockedStartTime;
    pthread_mutex_unlock(&_lock->mtx);
}

double cx_reslock_blocked_time(cx_reslock_t* _lock)
{
    return _lock->blockedTime;
}

uint16_t cx_reslock_counter(cx_reslock_t* _lock)
{
    uint16_t counter = 0;
    pthread_mutex_lock(&_lock->mtx);
    counter = _lock->counter;
    pthread_mutex_unlock(&_lock->mtx);
    return counter;
}