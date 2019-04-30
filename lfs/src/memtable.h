#ifndef LFS_MEMTABLE_H_
#define LFS_MEMTABLE_H_

#include "lfs.h"

#include <ker/defines.h>
#include <stdint.h>
#include <time.h>

#include <commons/collections/list.h>
#include <pthread.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool                memtable_init(const char* _tableName, bool _threadSafe, memtable_t* _outTable, cx_error_t* _err);

bool                memtable_init_from_dump(const char* _tableName, uint16_t _dumpNumber, bool _isDuringCompaction, memtable_t* _outTable, cx_error_t* _err);

bool                memtable_init_from_part(const char* _tableName, uint16_t _partNumber, bool _isDuringCompaction, memtable_t* _outTable, cx_error_t* _err);

void                memtable_destroy(memtable_t* _table);

void                memtable_add(memtable_t* _table, const table_record_t* _record, uint32_t _numRecords);

void                memtable_preprocess(memtable_t* _table);

bool                memtable_find(memtable_t* _table, uint16_t _key, table_record_t* _outRecord);

bool                memtable_make_dump(memtable_t* _table, cx_error_t* _err);

bool                memtable_make_part(memtable_t* _table, uint16_t _partNumber, cx_error_t* _err);

#endif // LFS_MEMTABLE_H_