#include "mm.h"

#include <cx/mem.h>
#include <cx/str.h>

#include <pthread.h>
#include <errno.h>

static mm_ctx_t*        m_mmCtx = NULL;

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void             _mm_page_to_record(uint16_t _pageHandle, table_record_t* _outRecord);

static void             _mm_record_to_page(table_record_t* _record, uint16_t _pageHandle);

static void             _mm_page_destroy(page_t* _page);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool mm_init(uint32_t _memSz, uint16_t _valueSz, cx_err_t* _err)
{
    CX_CHECK(NULL == m_mmCtx, "mm is already initialized!");

    bool success = false;

    m_mmCtx = CX_MEM_STRUCT_ALLOC(m_mmCtx);
    CX_ERR_CLEAR(_err);

    table_record_t r;
    m_mmCtx->valueSize = _valueSz;
    m_mmCtx->pageSize = sizeof(r.timestamp) + sizeof(r.key) + _valueSz;
    m_mmCtx->pageMax = _memSz / m_mmCtx->pageSize;

    if (0 >= m_mmCtx->pageMax)
    {
        CX_ERR_SET(_err, ERR_INIT_MM_PAGES, "not enough space to allocate a single page! (mainMemSize=%d pageSize=%d)", _memSz, m_mmCtx->pageSize);
        return false;
    }

    m_mmCtx->mainMem = malloc(m_mmCtx->pageMax * m_mmCtx->pageSize);
    if (NULL == m_mmCtx->mainMem)
    {
        CX_ERR_SET(_err, ERR_INIT_MM_MAIN, "failed to allocate main memory block of %d bytes!", m_mmCtx->pageMax * m_mmCtx->pageSize);
        return false;
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

    m_mmCtx->pagesHalloc = cx_halloc_init(m_mmCtx->pageMax);
    if (NULL == m_mmCtx->pagesHalloc)
    {
        CX_ERR_SET(_err, ERR_INIT_HALLOC, "pagesHalloc initialization failed!");
        return false;
    }

    if (0 != pthread_mutex_init(&m_mmCtx->pagesMtx, NULL))
    {
        CX_ERR_SET(_err, ERR_INIT_MTX, "pagesMtx initialization failed!");
        return false;
    }

    m_mmCtx->pagesLru = list_create();
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

    if (NULL != m_mmCtx->blockedQueue)
    {
        queue_destroy(m_mmCtx->blockedQueue);
        m_mmCtx->blockedQueue = NULL;
    }

    if (NULL != m_mmCtx->pagesHalloc)
    {
        cx_halloc_destroy(m_mmCtx->pagesHalloc);
        m_mmCtx->pagesHalloc = NULL;
    }
    
    if (NULL != m_mmCtx->pagesLru)
    {
        list_destroy(m_mmCtx->pagesLru);
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
    CX_CHECK(strlen(_tableName) > 0, "Invalid table name!");

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

void mm_segment_destroy(segment_t* _table)
{
    // this function is not thread-safe. it must only be called from the main thread!
    page_t* page = NULL;

    if (NULL != _table)
    {
        cx_reslock_destroy(&_table->reslock);

        if (NULL != _table->pages)
        {
            pthread_mutex_lock(&m_mmCtx->pagesMtx);
            cx_cdict_destroy(_table->pages, (cx_destroyer_cb)_mm_page_destroy);
            pthread_mutex_unlock(&m_mmCtx->pagesMtx);

            _table->pages = NULL;
        }

        free(_table);
    }
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

    pthread_mutex_lock(&m_mmCtx->pagesMtx);
    
    if (cx_handle_count(m_mmCtx->pagesHalloc) == cx_handle_capacity(m_mmCtx->pagesHalloc))
    {
        // get the LRU page and re-use it
        (*_outPage) = (page_t*)list_remove(m_mmCtx->pagesLru, list_size(m_mmCtx->pagesLru) - 1);
        
        pthread_rwlock_wrlock(&(*_outPage)->rwlock);
        (*_outPage)->modified = _isModification;
        (*_outPage)->parent = _parent;
        pthread_rwlock_unlock(&(*_outPage)->rwlock);
        
        //TODO should we notify here the parent that we stole his page??
        // this gets a little bit tricky since it could lead us to a deadlock
        // we should ask main thread to do this for us in some (synchronized) way

        success = true;
    }
    else
    {
        // grab a handle and allocate a new page for it
        uint16_t handle = cx_handle_alloc(m_mmCtx->pagesHalloc);
        if (INVALID_HANDLE != handle)
        {
            (*_outPage) = CX_MEM_STRUCT_ALLOC(*_outPage);
            (*_outPage)->handle = handle;
            (*_outPage)->modified = _isModification;
            (*_outPage)->parent = _parent;
            success = (0 == pthread_rwlock_init(&(*_outPage)->rwlock, NULL));
            
            if (!success)
            {
                // abort allocation
                cx_handle_free(m_mmCtx->pagesHalloc, handle);
                free(*_outPage);
                (*_outPage) = NULL;
                CX_ERR_SET(_err, ERR_GENERIC, "pthread_rwlock_init failed.");
            }
        }
        else
        {
            CX_ERR_SET(_err, ERR_GENERIC, "page handle allocation failed!");
        }
    }

    // if this is a replaceable page push it to the front of the list
    if (success && !_isModification)
    {
        list_add_in_index(m_mmCtx->pagesLru, 0, (*_outPage));
    }

    pthread_mutex_unlock(&m_mmCtx->pagesMtx);

    return success;
}

bool mm_page_read(segment_t* _table, uint16_t _key, table_record_t* _outRecord, cx_err_t* _err)
{
    bool success = false;
    page_t* page = NULL;
    char key[6];
    cx_str_from_uint16(_key, key, sizeof(key));

    if (cx_cdict_get(_table->pages, key, (void**)&page))
    {
        // this lock guarantees this page's content won't be modified while
        // we are reading it.
        pthread_rwlock_rdlock(&page->rwlock);
        // make sure the page is still assigned to this segment
        if (_table == page->parent) 
        {
            _mm_page_to_record(page->handle, _outRecord);
            success = true;
        }
        pthread_rwlock_unlock(&page->rwlock);
    }

    if (!success)
        CX_ERR_SET(_err, ERR_GENERIC, "Key %d does not exist in table '%s'.", _key, _table->tableName);

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
            _mm_page_to_record(page->handle, &curRecord);
            
            if (_record->timestamp > curRecord.timestamp)
            {
                // update the page with the given (most recent) value
                _mm_record_to_page(_record, page->handle);
                free(curRecord.value);
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
            _mm_record_to_page(_record, page->handle);
            pthread_rwlock_unlock(&page->rwlock);

            cx_cdict_set(_table->pages, key, page);

            success = true;
        }
    }
    pthread_mutex_unlock(&_table->pages->mtx);

    return success;
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static void _mm_page_to_record(uint16_t _pageHandle, table_record_t* _outRecord)
{
    const uint32_t keySz = sizeof(_outRecord->key);
    const uint32_t timestampSz = sizeof(_outRecord->timestamp);
    const uint32_t base = _pageHandle * m_mmCtx->pageSize;

    memcpy(&_outRecord->timestamp, &m_mmCtx->mainMem[base + 0], timestampSz);
    memcpy(&_outRecord->key, &m_mmCtx->mainMem[base + timestampSz], keySz);
    _outRecord->value = cx_str_copy_d(&m_mmCtx->mainMem[base + timestampSz + keySz]);
}

static void _mm_record_to_page(table_record_t* _record, uint16_t _pageHandle)
{
    const uint32_t keySz = sizeof(_record->key);
    const uint32_t timestampSz = sizeof(_record->timestamp);
    const uint32_t base = _pageHandle * m_mmCtx->pageSize;

    memcpy(&m_mmCtx->mainMem[base + 0], &_record->timestamp, timestampSz);
    memcpy(&m_mmCtx->mainMem[base + timestampSz], &_record->key, keySz);
    cx_str_copy(&m_mmCtx->mainMem[base + timestampSz + keySz], m_mmCtx->valueSize, _record->value);
}

static void _mm_page_destroy(page_t* _page)
{
    cx_handle_free(m_mmCtx->pagesHalloc, _page->handle);

    pthread_rwlock_destroy(&_page->rwlock);

    free(_page);
}