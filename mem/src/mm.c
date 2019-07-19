#include "mm.h"

#include <lfs/lfs_protocol.h>

#include <cx/mem.h>
#include <cx/str.h>
#include <cx/timer.h>

#include <pthread.h>
#include <errno.h>

static mm_ctx_t*        m_mmCtx = NULL;

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void             _mm_lru_push_front(page_t* _page);

static page_t*          _mm_lru_pop_back();

static void             _mm_lru_touch(page_t* _page);

static void             _mm_lru_remove(page_t* _page);

static void             _mm_lru_reset();

static void             _mm_frame_read(uint16_t _frameNumber, table_record_t* _outRecord);

static void             _mm_frame_write(uint16_t _frameNumber, table_record_t* _record);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool mm_init(uint32_t _memSz, uint16_t _valueSz, cx_err_t* _err)
{
    CX_CHECK(NULL == m_mmCtx, "mm is already initialized!");

    m_mmCtx = CX_MEM_STRUCT_ALLOC(m_mmCtx);
    CX_ERR_CLEAR(_err);

    table_record_t r;
    m_mmCtx->valueSize = _valueSz;
    m_mmCtx->frameSize = sizeof(r.timestamp) + sizeof(r.key) + _valueSz;
    m_mmCtx->frameMax = _memSz / m_mmCtx->frameSize;

    if (0 >= m_mmCtx->frameMax)
    {
        CX_ERR_SET(_err, ERR_INIT_MM_FRAMES, "not enough space to allocate a single frame! (mainMemSize=%d frameSize=%d)", _memSz, m_mmCtx->frameSize);
        return false;
    }

    m_mmCtx->mainMem = malloc(m_mmCtx->frameMax * m_mmCtx->frameSize);
    if (NULL == m_mmCtx->mainMem)
    {
        CX_ERR_SET(_err, ERR_INIT_MM_MAIN, "failed to allocate main memory block of %d bytes!", m_mmCtx->frameMax * m_mmCtx->frameSize);
        return false;
    }

    m_mmCtx->pagesCount = 0;
    m_mmCtx->pages = CX_MEM_ARR_ALLOC(m_mmCtx->pages, m_mmCtx->frameMax);
    if (NULL == m_mmCtx->pages)
    {
        CX_ERR_SET(_err, ERR_GENERIC, "page references array allocation failed!");
        return false;
    }
    
    for (uint32_t i = 0; i < m_mmCtx->frameMax; i++)
    {
        page_t* page = &m_mmCtx->pages[i];
        page->node = cx_list_node_alloc(page);

        if (NULL == page->node || (0 != pthread_rwlock_init(&page->rwlock, NULL)))
        {
            CX_ERR_SET(_err, ERR_GENERIC, "page references initialization failed!");
            return false;
        }
    }

    m_mmCtx->tablesMap = cx_cdict_init();
    if (NULL == m_mmCtx->tablesMap)
    {
        CX_ERR_SET(_err, ERR_INIT_CDICT, "tablesMap concurrent dictionary creation failed!");
        return false;
    }

    m_mmCtx->blockedQueue = queue_create();
    if (NULL == m_mmCtx->blockedQueue)
    {
        CX_ERR_SET(_err, ERR_INIT_RESLOCK, "blockedQueue creation failed!");
        return false;
    }

    if (!cx_reslock_init(&m_mmCtx->reslock, false))
    {
        CX_ERR_SET(_err, ERR_INIT_RESLOCK, "reslock initialization failed!");
        return false;
    }

    pthread_mutexattr_t attr;
    if (!(true
        && (0 == pthread_mutexattr_init(&attr))
        && (0 == pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE))
        && (0 == pthread_mutex_init(&m_mmCtx->pagesMtx, &attr))
        && (0 == pthread_mutexattr_destroy(&attr))))
    {
        CX_ERR_SET(_err, ERR_INIT_MTX, "pagesMtx initialization failed!");
        return false;
    }

    m_mmCtx->pagesLru = cx_list_init();
    if (NULL == m_mmCtx->pagesLru)
    {
        CX_ERR_SET(_err, ERR_INIT_LIST, "pagesLru list initialization failed!");
        return false;
    }

    return true;
}

void mm_destroy()
{
    if (NULL == m_mmCtx) return;

    if (NULL != m_mmCtx->mainMem)
    {
        free(m_mmCtx->mainMem);
        m_mmCtx->mainMem = NULL;
    }

    if (NULL != m_mmCtx->tablesMap)
    {
        cx_cdict_destroy(m_mmCtx->tablesMap, (cx_destroyer_cb)mm_segment_destroy);
        m_mmCtx->tablesMap = NULL;
    }

    if (NULL != m_mmCtx->pages)
    {
        for (uint32_t i = 0; i < m_mmCtx->frameMax; i++)
        {
            page_t* page = &m_mmCtx->pages[i];
            pthread_rwlock_destroy(&page->rwlock);

            if (NULL != page->node)
            {
                free(page->node);
                page->node = NULL;
            }
        }

        free(m_mmCtx->pages);
        m_mmCtx->pages = NULL;
    }

    if (NULL != m_mmCtx->blockedQueue)
    {
        queue_destroy(m_mmCtx->blockedQueue);
        m_mmCtx->blockedQueue = NULL;
    }
    
    if (NULL != m_mmCtx->pagesLru)
    {
        cx_list_destroy(m_mmCtx->pagesLru, NULL);
        m_mmCtx->pagesLru = NULL;
    }

    cx_reslock_destroy(&m_mmCtx->reslock);
    pthread_mutex_destroy(&m_mmCtx->pagesMtx);

    free(m_mmCtx);
}

bool mm_avail_guard_begin(cx_err_t* _err)
{
    bool available = cx_reslock_avail_guard_begin(&m_mmCtx->reslock);

    if (!available)
        CX_ERR_SET(_err, ERR_MEMORY_BLOCKED, "Operation cannot be performed at this time since the memory is blocked. Try agian later.");

    return available;
}

void mm_avail_guard_end()
{
    cx_reslock_avail_guard_end(&m_mmCtx->reslock);
}

bool mm_segment_init(segment_t** _outSegment, const char* _tableName, cx_err_t* _err)
{
    bool success = false;
    segment_t* table = CX_MEM_STRUCT_ALLOC(table);

    table->pages = cx_cdict_init();
    CX_CHECK_NOT_NULL(table->pages);

    cx_str_copy(table->tableName, sizeof(table->tableName), _tableName);

    success = true
        && (NULL != table->pages)
        && cx_reslock_init(&table->reslock, false);

    if (!success)
        CX_ERR_SET(_err, ERR_GENERIC, "Table initialization failed.")

    (*_outSegment) = success ? table : NULL;
    return success;
}

bool mm_segment_delete(const char* _tableName, segment_t** _outTable, cx_err_t* _err)
{
    segment_t* table = NULL;
    
    bool success = cx_cdict_tryremove(m_mmCtx->tablesMap, _tableName, (void**)_outTable);
    if (!success)
    {
        CX_ERR_SET(_err, 1, "Table '%s' does not exist.", _tableName);
    }

    return success;
}

void mm_segment_destroy(segment_t* _table)
{
    // this function is not thread-safe. it must only be called from mm_journal_run()

    page_t* page = NULL;

    CX_CHECK_NOT_NULL(_table);
    if (NULL == _table) return;

    if (NULL != _table->pages)
    {
        cx_cdict_destroy(_table->pages, NULL);
    }
    
    cx_reslock_destroy(&_table->reslock);
    free(_table);
}

bool mm_segment_exists(const char* _tableName, segment_t** _outTable)
{
    segment_t* table;

    if (cx_cdict_get(m_mmCtx->tablesMap, _tableName, (void**)&table))
    {
        if (NULL != _outTable)
        {
            (*_outTable) = table;
        }
        return true;
    }
    return false;
}

bool mm_segment_avail_guard_begin(const char* _tableName, cx_err_t* _err, segment_t** _outTable)
{
    bool available = false;
    (*_outTable) = NULL;

    pthread_mutex_lock(&m_mmCtx->tablesMap->mtx);
    if (!mm_segment_exists(_tableName, _outTable))
    {
        // insert the new segment right now
        if (mm_segment_init(_outTable, _tableName, _err))
        {
            cx_cdict_set(m_mmCtx->tablesMap, _tableName, *_outTable);
        }
    }

    if (NULL != (*_outTable))
    {
        available = cx_reslock_avail_guard_begin(&(*_outTable)->reslock);
        if (!available)
            CX_ERR_SET(_err, ERR_TABLE_BLOCKED, "Operation cannot be performed at this time since the table is blocked. Try agian later.");
    }
    pthread_mutex_unlock(&m_mmCtx->tablesMap->mtx);

    return available;
}

void mm_segment_avail_guard_end(segment_t* _table)
{
    cx_reslock_avail_guard_end(&_table->reslock);
}

bool mm_page_alloc(segment_t* _parent, bool _isModification, page_t** _outPage, cx_err_t* _err)
{
    bool success = false;
    page_t* page = NULL;

    pthread_mutex_lock(&m_mmCtx->pagesMtx);
    
    if (m_mmCtx->pagesCount == m_mmCtx->frameMax)
    {
        // get the LRU frame and re-use it
        page = _mm_lru_pop_back();

        if (NULL != page)
        {
            pthread_rwlock_wrlock(&page->rwlock);
            page->modified = _isModification;
            page->parent = _parent;
            pthread_rwlock_unlock(&page->rwlock);

            success = true;
        }
        else
        {
            CX_INFO("the memory is FULL.");
            CX_ERR_SET(_err, ERR_MEMORY_FULL, "the memory is full.");
        }
    }
    else
    {
        // grab a new frame
        page = &m_mmCtx->pages[m_mmCtx->pagesCount];
        page->frameNumber = m_mmCtx->pagesCount;
        page->modified = _isModification;
        page->parent = _parent;
        
        m_mmCtx->pagesCount++;
        success = true;
    }

    if (success && !_isModification)
    {
        // if this is a replaceable page push it to the front of the cache
        _mm_lru_push_front(page);
    }

    pthread_mutex_unlock(&m_mmCtx->pagesMtx);

    (*_outPage) = page;
    return success;
}

bool mm_page_read(segment_t* _table, uint16_t _key, table_record_t* _outRecord, cx_err_t* _err)
{
    bool success = false;
    page_t* page = NULL;
    char key[6];
    cx_str_from_uint16(_key, key, sizeof(key));

    pthread_mutex_lock(&_table->pages->mtx);
    if (cx_cdict_get(_table->pages, key, (void**)&page))
    {
        // this lock guarantees this page's content won't be modified while
        // we are reading it.
        pthread_rwlock_rdlock(&page->rwlock);
        // make sure the page is still assigned to this segment
        if (_table == page->parent) 
        {
            if (!page->modified)
            {
                _mm_lru_touch(page); // cache hit
            }

            _mm_frame_read(page->frameNumber, _outRecord);
            success = true;
        }
        pthread_rwlock_unlock(&page->rwlock);
    }
    pthread_mutex_unlock(&_table->pages->mtx);

    if (!success)
        CX_ERR_SET(_err, ERR_GENERIC, "Key %d does not exist in table '%s'.", _key, _table->tableName);

#ifdef DELAYS_ENABLED
    cx_time_sleep(g_ctx.cfg.delayMem);
#endif

    return success;
}

bool mm_page_write(segment_t* _table, table_record_t* _record, bool _isModification, cx_err_t* _err)
{
    bool success = false;

    char key[6];
    cx_str_from_uint16(_record->key, key, sizeof(key));

    page_t* page = NULL;

    pthread_mutex_lock(&_table->pages->mtx);
    if (cx_cdict_get(_table->pages, key, (void**)&page))
    {
        // this lock guarantees that this page's content won't be modified while
        // we are modifying it.
        pthread_rwlock_wrlock(&page->rwlock);
        // make sure the page is still assigned to this segment
        if (_table == page->parent)
        {
            table_record_t curRecord;
            _mm_frame_read(page->frameNumber, &curRecord);

            if (_record->timestamp >= curRecord.timestamp)
            {
                // update the page with the given (most recent) value
                _mm_frame_write(page->frameNumber, _record);
                free(curRecord.value);

                if (!page->modified && _isModification)
                {
                    _mm_lru_remove(page);
                }
                else if (page->modified && !_isModification)
                {
                    _mm_lru_push_front(page);
                }

                page->modified = _isModification;
            }
            else
            {
                // update the result of the request with the current (most recent) value
                free(_record->value);
                _record->value = curRecord.value;
            }

            success = true;
        }
        pthread_rwlock_unlock(&page->rwlock);
    }

    if (!success)
    {
        // we need to allocate a new page, write our record and insert it to the segment.
        if (mm_page_alloc(_table, _isModification, &page, _err))
        {
            pthread_rwlock_wrlock(&page->rwlock);
            _mm_frame_write(page->frameNumber, _record);
            pthread_rwlock_unlock(&page->rwlock);

            cx_cdict_set(_table->pages, key, page);

            success = true;
        }
    }
    pthread_mutex_unlock(&_table->pages->mtx);

#ifdef DELAYS_ENABLED
    cx_time_sleep(g_ctx.cfg.delayMem);
#endif

    return success;
}

void mm_reschedule_task(task_t* _task)
{
    if (ERR_MEMORY_FULL == _task->err.code)
    {
        mm_journal_tryenqueue();
    }

    if (ERR_MEMORY_FULL == _task->err.code
        || ERR_MEMORY_BLOCKED == _task->err.code)
    {
        queue_push(m_mmCtx->blockedQueue, _task);
        _task->state = TASK_STATE_BLOCKED_AWAITING;
    }
    else if (ERR_TABLE_BLOCKED == _task->err.code)
    {
        // I believe this only happens when we're about to drop it..
        // but in that case we shouldn't be really interested in rescheduling it.
        CX_WARN(CX_ALW, "we shouldn't be getting rescheduling requests on blocked tables!");
    }
}

bool mm_journal_tryenqueue()
{
    if (!m_mmCtx->journaling)
    {
        m_mmCtx->journaling = true;

        task_t* task = taskman_create(TASK_ORIGIN_INTERNAL_PRIORITY, TASK_WT_JOURNAL, NULL, INVALID_CID);
        if (NULL != task)
        {
            data_journal_t* data = CX_MEM_STRUCT_ALLOC(data);

            task->data = data;
            taskman_activate(task);
        }
        else
        {
            // task creation failed.
            m_mmCtx->journaling = false;
        }
    }
    else
    {
        // we can safely ignore this one
        // we don't really want multiple journals to be performed at the same time
        CX_INFO("Ignoring journal request (we're already performing journal in another thread).");
    }
    return true;
}

void mm_journal_run(task_t* _task)
{
    CX_CHECK(m_mmCtx->journaling, "this method should only be called when performing a memory journal!");
    CX_CHECK(TASK_WT_JOURNAL == _task->type, "this method should only be called when processing a TASK_WT_JOURNAL task!");

    bool        success = true;
    payload_t   payload;
    uint32_t    payloadSize = 0;
    segment_t*  table = NULL;
    page_t*     page = NULL;
    char*       tableName = NULL;
    int32_t     result = 0;
    table_record_t r;

    if (mm_block())
    {
        cx_cdict_iter_begin(m_mmCtx->tablesMap);
        while (ERR_NONE == _task->err.code && cx_cdict_iter_next(m_mmCtx->tablesMap, &tableName, (void**)&table))
        {
            cx_cdict_iter_begin(table->pages);
            while (ERR_NONE == _task->err.code && cx_cdict_iter_next(table->pages, NULL, (void**)&page))
            {
                if (page->parent == table && page->modified)
                {
                    _mm_frame_read(page->frameNumber, &r);

                    payloadSize = lfs_pack_req_insert(payload, sizeof(payload),
                        _task->handle, tableName, r.key, r.value, r.timestamp);

                    do
                    {
                        result = cx_net_send(g_ctx.lfs, LFSP_REQ_INSERT, payload, payloadSize, INVALID_CID);

                        if (CX_NET_SEND_DISCONNECTED == result)
                        {
                            CX_ERR_SET(&_task->err, ERR_NET_LFS_UNAVAILABLE, "LFS node is unavailable.");
                        }
                        else if (CX_NET_SEND_BUFFER_FULL == result)
                        {
                            cx_net_wait_outboundbuff(g_ctx.lfs, INVALID_CID, -1);
                        }
                    } while (ERR_NONE == _task->err.code && result != CX_NET_SEND_OK);

                    free(r.value);
                }
            }
            cx_cdict_iter_end(table->pages);
        }
        cx_cdict_iter_end(m_mmCtx->tablesMap);

        // destroy segments
        cx_cdict_clear(m_mmCtx->tablesMap, (cx_destroyer_cb)mm_segment_destroy);

        // destroy pages
        m_mmCtx->pagesCount = 0;

        // reset lru
        _mm_lru_reset();
        
        // unblock the resource
        mm_unblock(&((data_journal_t*)_task->data)->blockedTime);

        // unset the journaling flag
        m_mmCtx->journaling = false;
    }
    else
    {
        CX_CHECK(CX_ALW, "mm_block() should never return false!");
    }
}

bool mm_block()
{
    cx_reslock_block(&m_mmCtx->reslock);
    cx_reslock_wait_unused(&m_mmCtx->reslock);
    return true;
}

void mm_unblock(double* _blockedTime)
{
    // unblock the memory and make it available again
    cx_reslock_unblock(&m_mmCtx->reslock);

    task_t* task = NULL;
    while (!queue_is_empty(m_mmCtx->blockedQueue))
    {
        task = queue_pop(m_mmCtx->blockedQueue);
        taskman_activate(task);
    }

    if (NULL != _blockedTime)
        (*_blockedTime) = cx_reslock_blocked_time(&m_mmCtx->reslock);
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static void _mm_lru_push_front(page_t* _page)
{
    CX_CHECK_NOT_NULL(_page);
    pthread_mutex_lock(&m_mmCtx->pagesMtx);
    cx_list_push_front(m_mmCtx->pagesLru, _page->node);
    pthread_mutex_unlock(&m_mmCtx->pagesMtx);
}

static page_t* _mm_lru_pop_back()
{
    pthread_mutex_lock(&m_mmCtx->pagesMtx);
    cx_list_node_t* node = cx_list_pop_back(m_mmCtx->pagesLru);
    page_t* lruPage = node != NULL ? node->data : NULL;
    pthread_mutex_unlock(&m_mmCtx->pagesMtx);
    return lruPage;
}

static void _mm_lru_touch(page_t* _page)
{
    pthread_mutex_lock(&m_mmCtx->pagesMtx);
    cx_list_remove(m_mmCtx->pagesLru, _page->node);
    cx_list_push_front(m_mmCtx->pagesLru, _page->node);
    pthread_mutex_unlock(&m_mmCtx->pagesMtx);
}

static void _mm_lru_remove(page_t* _page)
{
    pthread_mutex_lock(&m_mmCtx->pagesMtx);
    cx_list_remove(m_mmCtx->pagesLru, _page->node);
    pthread_mutex_unlock(&m_mmCtx->pagesMtx);
}

static void _mm_lru_reset()
{
    pthread_mutex_lock(&m_mmCtx->pagesMtx);
    cx_list_clear(m_mmCtx->pagesLru, NULL);
    pthread_mutex_unlock(&m_mmCtx->pagesMtx);
}

static void _mm_frame_read(uint16_t _frameNumber, table_record_t* _outRecord)
{
    const uint32_t keySz = sizeof(_outRecord->key);
    const uint32_t timestampSz = sizeof(_outRecord->timestamp);
    const uint32_t base = _frameNumber * m_mmCtx->frameSize;

    memcpy(&_outRecord->timestamp, &m_mmCtx->mainMem[base + 0], timestampSz);
    memcpy(&_outRecord->key, &m_mmCtx->mainMem[base + timestampSz], keySz);
    _outRecord->value = cx_str_copy_d(&m_mmCtx->mainMem[base + timestampSz + keySz]);
}

static void _mm_frame_write(uint16_t _frameNumber, table_record_t* _record)
{
    const uint32_t keySz = sizeof(_record->key);
    const uint32_t timestampSz = sizeof(_record->timestamp);
    const uint32_t base = _frameNumber * m_mmCtx->frameSize;

    memcpy(&m_mmCtx->mainMem[base + 0], &_record->timestamp, timestampSz);
    memcpy(&m_mmCtx->mainMem[base + timestampSz], &_record->key, keySz);
    cx_str_copy(&m_mmCtx->mainMem[base + timestampSz + keySz], m_mmCtx->valueSize, _record->value);
}
