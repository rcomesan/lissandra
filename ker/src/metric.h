#ifndef KER_METRIC_H_
#define KER_METRIC_H_

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

typedef struct metric_t
{
    double              timeElapsed;    // total amount of time taken in seconds to perform the current amount of operations.
    uint32_t            hits;           // current amount of operations performed.
    pthread_mutex_t     mtx;            // mutex to allow access of multiple threads concurrently.
    bool                mtxInitialized; // true if mtx was successfully initialized.
} metric_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

metric_t*       metric_init();

void            metric_destroy(metric_t* _metric);

void            metric_hit(metric_t* _metric, double _timeElapsed);

void            metric_get(metric_t* _metric, uint32_t* _outHits, double* _outTimeElapsed);

void            metric_reset(metric_t* _metric);

#endif // KER_METRIC_H_