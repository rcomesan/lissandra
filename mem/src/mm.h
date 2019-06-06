#ifndef MEM_MM_H_
#define MEM_MM_H_

#include "mem.h"

#include <ker/taskman.h>

#include <cx/cx.h>
#include <cx/file.h>

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool                mm_init(uint32_t _memSz, uint16_t _valueSz, cx_err_t* _err);

void                mm_destroy();

bool                mm_avail_guard_begin(cx_err_t* _err);

void                mm_avail_guard_end();

bool                mm_segment_init(segment_t** _outSegment, const char* _tableName, cx_err_t* _err);

bool                mm_segment_delete(const char* _tableName, segment_t** _outTable, cx_err_t* _err);

void                mm_segment_destroy(segment_t* _table);

bool                mm_segment_exists(const char* _tableName, segment_t** _outTable);

bool                mm_segment_avail_guard_begin(const char* _tableName, cx_err_t* _err, segment_t** _outTable);

void                mm_segment_avail_guard_end(segment_t* _table);

bool                mm_page_alloc(segment_t* _parent, bool _isModification, page_t** _outPage, cx_err_t* _err);

bool                mm_page_read(segment_t* _table, uint16_t _key, table_record_t* _outRecord, cx_err_t* _err);

bool                mm_page_write(segment_t* _table, table_record_t* _record, bool _isModification, cx_err_t* _err);

void                mm_reschedule_task(task_t* _task);

bool                mm_journal_tryenqueue();

void                mm_journal_run(task_t* _task);

void                mm_unblock(double* _blockedTime);

#endif // MEM_MM_H_