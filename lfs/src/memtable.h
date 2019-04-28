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

bool                memtable_init(const char* _tableName, memtable_t* _outTable, cx_error_t* _err);

bool                memtable_init_from_dump(const char* _tableName, uint16_t _dumpNumber, bool _isDuringCompaction, memtable_t* _outTable, cx_error_t* _err);

bool                memtable_init_from_part(const char* _tableName, uint16_t _partNumber, memtable_t* _outTable, cx_error_t* _err);

void                memtable_destroy(memtable_t* _table);

void                memtable_add(memtable_t* _table, const table_record_t* _record);

bool                memtable_dump(memtable_t* _table, cx_error_t* _err);

bool                memtable_find(memtable_t* _table, uint16_t _key, table_record_t* _outRecord);

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/


#endif // LFS_MEMTABLE_H_