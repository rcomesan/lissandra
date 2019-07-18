#include "metric.h"

#include <cx/cx.h>
#include <cx/mem.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

metric_t* metric_init()
{
    metric_t* metric = CX_MEM_STRUCT_ALLOC(metric);

    if (NULL != metric)
    {
        metric->mtxInitialized = (0 == pthread_mutex_init(&metric->mtx, NULL));
        if (!metric->mtxInitialized)
        {
            CX_WARN(CX_ALW, "metric mutex initialization failed!");
            metric_destroy(metric);
            return NULL;
        }
    }

    return metric;
}

void metric_destroy(metric_t* _metric)
{
    if (NULL == _metric) return;

    if (_metric->mtxInitialized)
    {
        pthread_mutex_destroy(&_metric->mtx);
        _metric->mtxInitialized = false;
    }

    free(_metric);
}

void metric_hit(metric_t* _metric, double _timeElapsed)
{
    pthread_mutex_lock(&_metric->mtx);

    _metric->hits++;
    _metric->timeElapsed += _timeElapsed;

    pthread_mutex_unlock(&_metric->mtx);
}

void metric_get(metric_t* _metric, uint32_t* _outHits, double* _outTimeElapsed)
{
    pthread_mutex_lock(&_metric->mtx);

    if (NULL != _outHits) 
        (*_outHits) = _metric->hits;

    if (NULL != _outTimeElapsed) 
        (*_outTimeElapsed) = _metric->timeElapsed;

    pthread_mutex_unlock(&_metric->mtx);
}

void metric_reset(metric_t* _metric)
{
    pthread_mutex_lock(&_metric->mtx);

    _metric->hits = 0;
    _metric->timeElapsed = 0;

    pthread_mutex_unlock(&_metric->mtx);
}