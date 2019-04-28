#include "memtable.h"
#include "fs.h"

#include <cx/str.h>
#include <cx/mem.h>
#include <cx/sort.h>
#include <cx/math.h>

#include <string.h>

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool         _memtable_init(const char* _tableName, memtable_t* _outTable, cx_error_t* _err);

static int32_t      _memtable_comp_full(const void* _a, const void* _b, void* _userData);

static int32_t      _memtable_comp_basic(const void* _a, const void* _b, void* _userData);

static bool         _memtable_save(memtable_t* _table, fs_file_t* _outFile, cx_error_t* _err);

static bool         _memtable_load(memtable_t* _table, char* _buff, uint32_t _buffSize, cx_error_t* _err);

static void         _memtable_record_destroyer(void* _data);


/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool memtable_init(const char* _tableName, memtable_t* _outTable, cx_error_t* _err)
{
    if (!_memtable_init(_tableName, _outTable, _err)) return false;

    _outTable->type = MEMTABLE_TYPE_MEM;
    _outTable->recordsSorted = false;

    _outTable->mtxInitialized = (0 == pthread_mutex_init(&_outTable->mtx, NULL));
    CX_CHECK(_outTable->mtxInitialized, "mutex initialization failed!");

    return true;
}

bool memtable_init_from_dump(const char* _tableName, uint16_t _dumpNumber, bool _isDuringCompaction, memtable_t* _outTable, cx_error_t* _err)
{
    if (!_memtable_init(_tableName, _outTable, _err)) return false;
    
    _outTable->type = MEMTABLE_TYPE_DISK;
    _outTable->recordsSorted = true;

    fs_file_t dumpFile;
    if (fs_table_get_dump(_tableName, _dumpNumber, _isDuringCompaction, &dumpFile, _err))
    {
        char* buff = malloc(dumpFile.size);
        if (fs_file_load(&dumpFile, buff, _err))
        {
            _memtable_load(_outTable, buff, dumpFile.size, _err);
        }
        free(buff);
    }

    if (ERR_NONE != _err->code) memtable_destroy(_outTable);
    return (ERR_NONE == _err->code);
}

bool memtable_init_from_part(const char* _tableName, uint16_t _partNumber, memtable_t* _outTable, cx_error_t* _err)
{
    if (!_memtable_init(_tableName, _outTable, _err)) return false;   

    _outTable->type = MEMTABLE_TYPE_DISK;
    _outTable->recordsSorted = true;

    fs_file_t partFile;
    if (fs_table_get_part(_tableName, _partNumber, &partFile, _err))
    {
        char* buff = malloc(partFile.size);
        if (fs_file_load(&partFile, buff, _err))
        {
            _memtable_load(_outTable, buff, partFile.size, _err);
        }
        free(buff);
    }

    if (ERR_NONE != _err->code) memtable_destroy(_outTable);
    return (ERR_NONE == _err->code);
}

void memtable_destroy(memtable_t* _table)
{
    CX_CHECK_NOT_NULL(_table);

    if (MEMTABLE_TYPE_MEM == _table->type)
    {
        CX_WARN(0 == _table->recordsCount, "destroying memtable from table '%s' with %d entries pending to be dumped!", 
            _table->name, _table->recordsCount);
    }

    for (uint32_t i = 0; i < _table->recordsCount; i++)
    {
        free(_table->records[i].value);
        _table->records[i].value = NULL;
    }
    free(_table->records);
    _table->records = NULL;

    if (_table->mtxInitialized)
    {
        pthread_mutex_destroy(&_table->mtx);
        _table->mtxInitialized = false;
    }
}

void memtable_add(memtable_t* _table, const table_record_t* _record)
{
    CX_CHECK_NOT_NULL(_table);

    if (_table->mtxInitialized) pthread_mutex_lock(&_table->mtx);

    if (_table->recordsCount == _table->recordsCapacity)
    {
        // we need more extra space, reallocate our records array doubling its capacity
        _table->recordsCapacity *= 2;
        _table->records = CX_MEM_ARR_REALLOC(_table->records, _table->recordsCapacity);
        if (NULL == _table->records) return; // oom. ignore the request.
    }

    _table->records[_table->recordsCount].timestamp = _record->timestamp;
    _table->records[_table->recordsCount].key = _record->key;
    _table->records[_table->recordsCount].value = cx_str_copy_d(_record->value);

    _table->recordsCount++;

    if (_table->mtxInitialized) pthread_mutex_unlock(&_table->mtx);
}

bool memtable_dump(memtable_t* _table, cx_error_t* _err)
{
    CX_CHECK_NOT_NULL(_table);
    CX_CHECK(MEMTABLE_TYPE_MEM == _table->type, "you can only dump memtables of type MEM!")

    pthread_mutex_lock(&_table->mtx);
    
    // we want to pre-process our memtable in a way that we end up with a set of unique values
    // sorted by partition number (asc), key (asc) and timestamp (desc).
    // this will allow us to do binary searches during select and compaction operations.

    table_t* table = NULL;
    if (fs_table_exists(_table->name, &table))
    {        
        cx_sort_quick(_table->records, sizeof(_table->records[0]),
            _table->recordsCount, _memtable_comp_full, table);

        _table->recordsCount = cx_sort_uniquify(_table->records, sizeof(_table->records[0]), 
            _table->recordsCount, _memtable_comp_basic, table, _memtable_record_destroyer);

        fs_file_t dumpFile;
        CX_MEM_ZERO(dumpFile);
        if (_memtable_save(_table, &dumpFile, _err))
        {
            uint16_t dumpNumber = fs_table_get_dump_number_next(_table->name);
            if (fs_table_set_dump(_table->name, dumpNumber, &dumpFile, _err))
            {
                // clear the memtable
                for (uint32_t i = 0; i < _table->recordsCount; i++)
                {
                    _memtable_record_destroyer((void*)&_table->records[i]);
                }
                _table->recordsCount = 0;
            }
        }
    }
    else
    {
        CX_ERROR_SET(_err, 1, "Table '%s' does not exist.", _table->name);
    }
       
    pthread_mutex_unlock(&_table->mtx);
    return (ERR_NONE == _err->code);
}

bool memtable_find(memtable_t* _table, uint16_t _key, table_record_t* _outRecord)
{
    if (_table->mtxInitialized) pthread_mutex_lock(&_table->mtx);

    int32_t pos = -1;
    bool found = false;

    if (MEMTABLE_TYPE_DISK == _table->type)
    {
        // binary search. (assume the entries in our files are sorted and contain no duplicates)
        table_t* table = NULL;
        if (fs_table_exists(_table->name, &table))
        {
            table_record_t keyRecord = { _key, 0, "" };
            pos = cx_sort_find(_table->records, sizeof(table_record_t),
                _table->recordsCount, &keyRecord, false, _memtable_comp_basic, table);
        }
    }
    else if (MEMTABLE_TYPE_MEM == _table->type)
    {
        // linear search ( we don't have our records sorted yet :'c )

        uint32_t highestTimestamp = 0;
        for (uint32_t i = 0; i < _table->recordsCount; i++)
        {
            if (_table->records[i].key == _key
                && _table->records[i].timestamp > highestTimestamp)
            {
                pos = i;
                highestTimestamp = _table->records[i].timestamp;
            }
        }
    }
    else
    {
        CX_WARN(CX_ALW, "undefined memtable find behaviour for type #%d", _table->type);
    }

    if (pos >= 0)
    {
        found = true;
        memcpy(_outRecord, &_table->records[pos], sizeof(*_outRecord));
    }

    if (_table->mtxInitialized) pthread_mutex_unlock(&_table->mtx);
    return found;
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static bool _memtable_init(const char* _tableName, memtable_t* _outTable, cx_error_t* _err)
{
    CX_CHECK_NOT_NULL(_outTable);
    int32_t tableNameLen = strlen(_tableName);
    CX_WARN(tableNameLen <= TABLE_NAME_LEN_MAX, "table name truncated. maximum table name length is %d characters.", TABLE_NAME_LEN_MAX);
    CX_WARN(tableNameLen >= TABLE_NAME_LEN_MIN, "memtable_create ignored. the table name must have at least %d characters", TABLE_NAME_LEN_MIN);
    if (tableNameLen < TABLE_NAME_LEN_MIN)
    {
        CX_ERROR_SET(_err, 1, "Invalid table name '%s'.", _tableName);
        return false;
    }

    CX_MEM_ZERO(*_outTable);
    cx_str_copy(_outTable->name, sizeof(_outTable->name), _tableName);
    _outTable->recordsCount = 0;
    _outTable->recordsCapacity = MEMTABLE_INITIAL_CAPACITY;
    _outTable->records = CX_MEM_ARR_ALLOC(_outTable->records, _outTable->recordsCapacity);

    return true;
}

static int32_t _memtable_comp_full(const void* _a, const void* _b, void* _userData)
{
    // compare and determine the position of _a relative to _b
    //
    // returning a negative number means _a must be placed to the left of _b.
    // returning a positive number means _a must be placed to the right of _b.
    // returning zero means _a and _b are considered equal to each other even 
    // though they might not be.
    //
    // we'll sort first by partition number in ascending order,
    // then by key in ascending order, 
    // and then by timestamp in descending order.
    //
    // for instance, consider the following table with 
    // partitionsCount = 6.
    //
    //  [ 0 ]  12: "Red"    ; 945869  <--- right value for key #12
    //  [ 0 ]  12: "Green"  ; 578340  
    //  [ 1 ]  49: "Blue"   ; 195834  <--- right value for key #49
    //  [ 1 ]  49: "Orange" ; 146001  
    //  [ 1 ]  49: "Yellow" ; 116001  
    //  [ 4 ]  52: "Cyan"   ; 145869  <--- right value for key #52  
    //  [ 4 ] 106: "Black"  ; 744543  <--- right value for key #106  
    //  [ 5 ]  77: "White"  ; 969580  <--- right value for key #77

    table_record_t* a = ((table_record_t*)_a);
    table_record_t* b = ((table_record_t*)_b);
    table_t* table = (table_t*)_userData;
    int32_t result = 0;

    // 1) compare partition numbers
    result = (a->key % table->meta.partitionsCount) - (b->key % table->meta.partitionsCount);
    if (result > 0) return  1;  // _a has a partition number greater than _b.
    if (result < 0) return -1;  // _a has a partition number lower   than _b.

    // 2) compare keys
    result = a->key - b->key;
    if (result > 0) return  1;  // _a has a key greater than _b.
    if (result < 0) return -1;  // _a has a key lower   than _b.

    // 3) keys are equal. now compare timestamps.
    result = b->timestamp - a->timestamp;
    if (result > 0) return  1;  // _b has _a timestamp greater than _a (is more recent)
    if (result < 0) return -1;  // _b has _a timestamp lower   than _a (is less recent)

    // a and b are considered to be equal
    return 0;
}

static int32_t _memtable_comp_basic(const void* _a, const void* _b, void* _userData)
{
    table_record_t* a = ((table_record_t*)_a);
    table_record_t* b = ((table_record_t*)_b);
    table_t* table = (table_t*)_userData;
    int32_t result = 0;

    // 1) compare partition numbers
    result = (a->key % table->meta.partitionsCount) - (b->key % table->meta.partitionsCount);
    if (result > 0) return  1;  // _a has a partition number greater than _b.
    if (result < 0) return -1;  // _a has a partition number lower   than _b.

    // 2) compare keys
    result = a->key - b->key;
    if (result > 0) return  1;  // _a has a key greater than _b.
    if (result < 0) return -1;  // _a has a key lower   than _b.

    // a and b are considered to be equal
    return 0; 
}

static void _memtable_record_destroyer(void* _data)
{
    table_record_t* record = (table_record_t*)_data;
    free(record->value);
    record->value = NULL;
}

static bool _memtable_save(memtable_t* _table, fs_file_t* _outFile, cx_error_t* _err)
{
    // serializes and stores the serialized representation of the given 
    // memtable with the format [TIMESTAMP];[KEY];[VALUE] in the LFS.

    // if this function returns true, _outFile is the resulting filesystem file 
    // with all the blocks allocated and written with the serialized memtable.
       
    if (_table->recordsCount <= 0) return false;

    CX_MEM_ZERO(*_outFile);

    uint32_t tmpSize = g_ctx.cfg.valueSize + 40;
    uint32_t tmpLen = 0;
    char*    tmp = malloc(tmpSize);

    uint32_t buffSize = fs_block_size();
    uint32_t buffPos = 0;
    char*    buff = malloc(buffSize);

    uint32_t writableBytes = 0;
    uint32_t remainingBytes = 0;

    // allocate an initial block
    if (1 == fs_block_alloc(1, &_outFile->blocks[_outFile->blocksCount]))
    {
        _outFile->blocksCount++;

        for (uint32_t i = 0; i < _table->recordsCount; i++)
        {
            tmpLen = snprintf(tmp, tmpSize, "%u;%u;%s;",
                _table->records[i].timestamp,
                _table->records[i].key,
                _table->records[i].value);
            
            if (tmpLen >= tmpSize)
            {
                // ensure the records always terminate with a semicolon, even if our temp 
                // buffer is not enough and the value is truncated.
                // it's not really our fault, the value has a length greater than the one 
                // allowed (specified in the config)
                CX_CHECK(CX_ALW, "temp buffer for table '%s' is not enough! the length of the value is %d but the maximum allowed is %d",
                    _table->name, strlen(_table->records[i].value), g_ctx.cfg.valueSize);
                tmpLen = tmpSize - 1;
                tmp[tmpLen - 1] = ';'; // ensure trailing semicolon
            }

            // increment the total file size
            _outFile->size += tmpLen;

            // write as many bytes as possible into our buffer (depending on capacity remaining)
            writableBytes = cx_math_min(buffSize - buffPos, tmpLen);
            remainingBytes = tmpLen - writableBytes;

            // copy the serialized record from temp to our buffer
            memcpy(&buff[buffPos], tmp, writableBytes);
            buffPos += writableBytes;

            // if we reached the end of the current block, flush it to disk and grab a new one
            if (buffPos == buffSize)
            {
                if (!fs_block_write(_outFile->blocks[_outFile->blocksCount - 1], buff, buffSize, _err))
                    goto failed;

                if (1 == fs_block_alloc(1, &_outFile->blocks[_outFile->blocksCount]))
                {
                    _outFile->blocksCount++;

                    if (remainingBytes > 0)
                    {
                        // we didnt't have enough space to write our whole record serialized
                        // let's recover those remaining bytes and put them at the begining of our buffer
                        memmove(buff, &tmp[writableBytes], remainingBytes);
                        buffPos = remainingBytes;
                    }
                    else
                    {
                        buffPos = 0;
                    }
                }
                else
                {
                    CX_ERROR_SET(_err, 1, "lfs block allocation failed!");
                    goto failed;
                }
            }
        }

        if (buffPos > 0)
        {
            // flush our incomplete buffer to disk
            if (!fs_block_write(_outFile->blocks[_outFile->blocksCount - 1], buff, buffPos, _err))
                goto failed;
        }
        else
        {
            // free the latest allocated block (we don't really need it, since our buff is empty!)
            _outFile->blocksCount--;
            fs_block_free(&_outFile->blocks[_outFile->blocksCount], 1);
        }
    }
    else
    {
        CX_ERROR_SET(_err, 1, "lfs block allocation failed!");
    }

failed:
    if (ERR_NONE != _err->code)
    {
        // the serialization failed, free the blocks allocated (if any)
        fs_block_free(_outFile->blocks, _outFile->blocksCount);
    }
    
    free(buff);
    free(tmp);
    return (ERR_NONE == _err->code);
}

static bool _memtable_load(memtable_t* _table, char* _buff, uint32_t _buffSize, cx_error_t* _err)
{
    CX_CHECK(MEMTABLE_TYPE_DISK == _table->type, "you can only parse buffers from memtables of type DISK!");
    
#define UINT32_MAX_CHARS 10

    uint8_t  partCount = 0;

    uint32_t tmpSize = cx_math_max(g_ctx.cfg.valueSize, UINT32_MAX_CHARS) + 1;
    uint32_t tmpPos = 0;
    char*    tmp = malloc(tmpSize);

    for (uint32_t i = 0; i < _buffSize; i++)
    {
        if (';' == _buff[i])
        {
            if (i <= 0) goto ignore_token;

            partCount++;
            tmp[tmpPos] = '\0';

            if (1 == partCount)
            {
                // a new record just started.
                // if our container is full, make some extra space.
                if (_table->recordsCount == _table->recordsCapacity)
                {
                    _table->recordsCapacity *= 2;
                    _table->records = CX_MEM_ARR_REALLOC(_table->records, _table->recordsCapacity);
                    if (NULL == _table->records)
                    {
                        CX_ERROR_SET(_err, 1, "oom. records array reallocation with %d elements failed!", _table->recordsCapacity);
                        break; // we're in trouble.
                    }
                }

                // initialize and parse the timestamp.
                cx_str_to_uint32(tmp, &_table->records[_table->recordsCount].timestamp);
                _table->records[_table->recordsCount].value = NULL;
            }
            else if (2 == partCount)
            {
                // second part of the record (the key).
                cx_str_to_uint16(tmp, &_table->records[_table->recordsCount].key);
            }
            else if (3 == partCount)
            {
                // third and last part of the record (the value).
                _table->records[_table->recordsCount].value = cx_str_copy_d(tmp);
                partCount = 0;
                _table->recordsCount++;
            }

            tmpPos = 0;
        }
        else
        {
            tmp[tmpPos++] = _buff[i];
        }
    ignore_token:;
    }

    free(tmp);
    return (ERR_NONE == _err->code);
}