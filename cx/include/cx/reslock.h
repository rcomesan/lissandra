#ifndef CX_RESLOCK_H_
#define CX_RESLOCK_H_

#include "cx.h"

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct cx_reslock_t
{
    bool                blocked;            // true if the resource is blocked. when a resource is blocked, all the calls to cx_avail_guard_begin will fail.
    double              blockedStartTime;   // time counter value of when the table block started.
    double              blockedTime;        // lastest amount of time in blocked status.
    uint16_t            counter;            // number of operations being performed on this resource (number of threads which depend on its availability).
    pthread_mutex_t     mtx;                // mutex for making operations thread safe.
    pthread_cond_t      cond;               // condition to signal threads to wake up when the reslock counter reaches zero.
} cx_reslock_t;

bool cx_reslock_init(cx_reslock_t* _lock, bool _startsBlocked);

void cx_reslock_destroy(cx_reslock_t* _lock);

bool cx_reslock_avail_guard_begin(cx_reslock_t* _lock);

void cx_reslock_avail_guard_end(cx_reslock_t* _lock);

bool cx_reslock_is_blocked(cx_reslock_t* _lock);

void cx_reslock_block(cx_reslock_t* _lock);

void cx_reslock_unblock(cx_reslock_t* _lock);

double cx_reslock_blocked_time(cx_reslock_t* _lock);

uint16_t cx_reslock_counter(cx_reslock_t* _lock);

void cx_reslock_wait_unused(cx_reslock_t* _lock);

#endif // CX_RESLOCK_H_