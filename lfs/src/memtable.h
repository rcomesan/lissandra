#ifndef MEMTABLE_H_
#define MEMTABLE_H_

#include "lfs.h"

#include <stdint.h>
#include <time.h>

typedef struct record_t
{
    uint16_t        key;
    int64_t         timestamp;
    char*           value;
} record_t;

typedef struct memtable_t
{
    char            name[TABLE_NAME_LEN_MAX + 1];
    record_t*       records;
    uint16_t        recordsCount;
    uint16_t        recordsCapacity;
} memtable_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool                memtable_init();

void                memtable_terminate();

memtable_t*         memtable_create(const char* _tableName);

void                memtable_destroy(memtable_t* _table);

void                memtable_add(memtable_t* _table, uint16_t _key, const char* _value, int64_t _timestamp);

void                memtable_dump(memtable_t* _table);

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/


#endif // MEMTABLE_H_