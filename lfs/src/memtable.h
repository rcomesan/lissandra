#ifndef LFS_MEMTABLE_H_
#define LFS_MEMTABLE_H_

#include <ker/defines.h>

#include <stdint.h>
#include <time.h>

#include <commons/collections/list.h>
#include <pthread.h>

typedef struct memtable_record_t
{
    uint16_t            key;
    int64_t             timestamp;
    char*               value;
} memtable_record_t;

typedef struct memtable_t
{
    table_name_t        name;
    pthread_mutex_t     mtx;
    bool                mtxInitialized;             // true if mtx was successfully initialized and therefore needs to be destroyed.
    memtable_record_t*  records;                    // array for storing memtable entries
    uint32_t            recordsCount;               // number of elements in our array
    uint32_t            recordsCapacity;            // total capacity of our array
} memtable_t;

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

memtable_t*         memtable_init(const char* _tableName);

void                memtable_destroy(memtable_t* _table);

void                memtable_add(memtable_t* _table, uint16_t _key, const char* _value, uint32_t _timestamp);

void                memtable_dump(memtable_t* _table);

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/


#endif // LFS_MEMTABLE_H_