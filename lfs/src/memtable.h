#ifndef LFS_MEMTABLE_H_
#define LFS_MEMTABLE_H_

#include <ker/defines.h>

#include <stdint.h>
#include <time.h>

typedef struct memtable_record_t
{
    uint16_t            key;
    int64_t             timestamp;
    char*               value;
} memtable_record_t;

typedef struct memtable_t
{
    table_name_t        name;
    memtable_record_t*  records;
    uint16_t            recordsCount;
    uint16_t            recordsCapacity;
} memtable_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

memtable_t*         memtable_init(const char* _tableName);

void                memtable_destroy(memtable_t* _table);

void                memtable_add(memtable_t* _table, uint16_t _key, const char* _value, int64_t _timestamp);

void                memtable_dump(memtable_t* _table);

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/


#endif // LFS_MEMTABLE_H_