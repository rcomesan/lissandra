#include "memtable.h"

#include <cx/str.h>
#include <cx/mem.h>

#include <string.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

memtable_t* memtable_create(const char* _tableName)
{
    int32_t tableNameLen = strlen(_tableName);
    CX_WARN(tableNameLen <= TABLE_NAME_LEN_MAX, "table name truncated. maximum table name length is %d characters.", TABLE_NAME_LEN_MAX);
    CX_WARN(tableNameLen >= TABLE_NAME_LEN_MIN, "memtable_create ignored. the table name must have at least %d characters", TABLE_NAME_LEN_MIN);
    if (tableNameLen <= 0) return NULL;

    memtable_t* table = CX_MEM_STRUCT_ALLOC(table);
    cx_str_copy(table->name, sizeof(table->name), _tableName);
    table->recordsCount = 0;
    table->recordsCapacity = MEMTABLE_INITIAL_CAPACITY;
    table->records = CX_MEM_ARR_ALLOC(table->records, table->recordsCapacity);

    return table;
}

void memtable_destroy(memtable_t* _table)
{
    CX_CHECK_NOT_NULL(_table);

    for (int32_t i = 0; i < _table->recordsCount; i++)
    {
        free(_table->records[i].value);
    }
    free(_table->records);
    free(_table);
}

void memtable_add(memtable_t* _table, uint16_t _key, const char* _value, int64_t _timestamp)
{
    CX_CHECK_NOT_NULL(_table);

    if (_table->recordsCount == _table->recordsCapacity)
    {
        // we need more extra space, reallocate our records array doubling its capacity
        _table->recordsCapacity *= 2;
        _table->records = CX_MEM_ARR_REALLOC(_table->records, _table->recordsCapacity);
        if (NULL == _table->records) return; // oom. ignore the request.
    }

    _table->records[_table->recordsCount].timestamp = _timestamp;
    _table->records[_table->recordsCount].key = _key;
    _table->records[_table->recordsCount].value = cx_str_copy_d(_value);

    _table->recordsCount++;
}

void memtable_dump(memtable_t* _table)
{
    CX_CHECK_NOT_NULL(_table);
    //TODO dump process here
    // this should generate exactly one *.tmp file per table per call
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/