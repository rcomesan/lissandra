#include <ker/common_protocol.h>

#include <cx/binr.h>
#include <cx/binw.h>
#include <cx/mem.h>
#include <cx/str.h>

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void         _common_pack_err(char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, const cx_err_t* _err);

static void         _common_pack_res_generic(char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t _remoteId, const cx_err_t* _err);

static void         _common_unpack_err(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, cx_err_t* _err);

static void         _common_unpack_res_generic(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, cx_err_t* _err);

/****************************************************************************************
 ***  COMMON MESSAGE PACKERS
 ***************************************************************************************/

uint32_t common_pack_req_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval)
{
    uint32_t pos = 0;
    common_pack_remote_id(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    cx_binw_uint8(_buffer, _size, &pos, _consistency);
    cx_binw_uint16(_buffer, _size, &pos, _numPartitions);
    cx_binw_uint32(_buffer, _size, &pos, _compactionInterval);
    return pos;
}

uint32_t common_pack_req_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    uint32_t pos = 0;
    common_pack_remote_id(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    return pos;
}

uint32_t common_pack_req_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    uint32_t pos = 0;
    common_pack_remote_id(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, (NULL != _tableName) ? _tableName : "");
    return pos;
}

uint32_t common_pack_req_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key)
{
    uint32_t pos = 0;
    common_pack_remote_id(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    cx_binw_uint16(_buffer, _size, &pos, _key);
    return pos;
}

uint32_t common_pack_req_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint64_t _timestamp)
{
    uint32_t pos = 0;
    common_pack_remote_id(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    cx_binw_uint16(_buffer, _size, &pos, _key);
    cx_binw_str(_buffer, _size, &pos, _value);
    cx_binw_uint64(_buffer, _size, &pos, _timestamp);
    return pos;
}

uint32_t common_pack_res_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err)
{
    uint32_t pos = 0;
    _common_pack_res_generic(_buffer, _size, &pos, _remoteId, _err);
    return pos;
}

uint32_t common_pack_res_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err)
{
    uint32_t pos = 0;
    _common_pack_res_generic(_buffer, _size, &pos, _remoteId, _err);
    return pos;
}

bool common_pack_res_describe(char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t _remoteId, table_meta_t* _tables, uint16_t _tablesCount, uint16_t* _tablesPacked, const cx_err_t* _err)
{
    // reset the buffer position. each call to this method assumes a new packet will be send
    (*_bufferPos) = 0;

    common_pack_remote_id(_buffer, _bufferSize, _bufferPos, _remoteId);

    if ((*_tablesPacked) == 0) // first packet, send the total amount
        cx_binw_uint16(_buffer, _bufferSize, _bufferPos, _tablesCount);

    if (1 == _tablesCount)
    {
        _common_pack_err(_buffer, _bufferSize, _bufferPos, _err);
        if (ERR_NONE != _err->code)
        {
            return true; // finished packing the only 1 element (failed describe).
        }
    }

    // start sending them in chunks
    payload_t tmp;
    uint32_t  tableSize = 0;

    for (uint16_t i = (*_tablesPacked); i < _tablesCount; i++)
    {
        // pack this table's metadata into tmp
        tableSize = common_pack_table_meta(tmp, sizeof(tmp), &_tables[i]);

        if (tableSize > _bufferSize - (*_bufferPos))
        {
            // not enough space to append this one            
            return false; // the packing is not yet complete
        }

        // append it
        memcpy(&_buffer[*_bufferPos], tmp, tableSize);
        (*_bufferPos) = (*_bufferPos) + tableSize;
        (*_tablesPacked) = (*_tablesPacked) + 1;
    }

    return true;
}

uint32_t common_pack_res_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err, const table_record_t* _record)
{
    uint32_t pos = 0;
    _common_pack_res_generic(_buffer, _size, &pos, _remoteId, _err);
    if (ERR_NONE == _err->code)
    {
        pos += common_pack_table_record(&_buffer[pos], _size - pos, _record);
    }
    return pos;
}

uint32_t common_pack_res_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const cx_err_t* _err)
{
    uint32_t pos = 0;
    _common_pack_res_generic(_buffer, _size, &pos, _remoteId, _err);
    return pos;
}

uint32_t common_pack_table_meta(char* _buffer, uint16_t _size, const table_meta_t* _table)
{
    uint32_t pos = 0;
    cx_binw_str(_buffer, _size, &pos, _table->name);
    cx_binw_uint8(_buffer, _size, &pos, _table->consistency);
    cx_binw_uint16(_buffer, _size, &pos, _table->partitionsCount);
    cx_binw_uint32(_buffer, _size, &pos, _table->compactionInterval);
    return pos;
}

uint32_t common_pack_table_record(char* _buffer, uint16_t _size, const table_record_t* _record)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _record->key);
    cx_binw_str(_buffer, _size, &pos, _record->value);
    cx_binw_uint64(_buffer, _size, &pos, _record->timestamp);
    return pos;
}

void common_pack_remote_id(char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t _remoteId)
{
    cx_binw_uint16(_buffer, _bufferSize, _bufferPos, _remoteId);
}

void common_unpack_remote_id(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId)
{
    if (NULL != _outRemoteId)
    {
        // only unpack the remoteId if requested (pointer is not NULL)
        // in some cases such as in RES_BEGIN/RES_END macros, remoteId is already 
        // consumed from the stream to assign the response result to the requester (task).
        cx_binr_uint16(_buffer, _bufferSize, _bufferPos, _outRemoteId);
    }
}

/****************************************************************************************
 ***  COMMON MESSAGE UNPACKERS
 ***************************************************************************************/

data_create_t* common_unpack_req_create(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId)
{
    data_create_t* data = CX_MEM_STRUCT_ALLOC(data);
    common_unpack_remote_id(_buffer, _bufferSize, _bufferPos, _outRemoteId);
    cx_binr_str(_buffer, _bufferSize, _bufferPos, data->tableName, sizeof(data->tableName));
    cx_binr_uint8(_buffer, _bufferSize, _bufferPos, &data->consistency);
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, &data->numPartitions);
    cx_binr_uint32(_buffer, _bufferSize, _bufferPos, &data->compactionInterval);
    return data;
}

data_drop_t* common_unpack_req_drop(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId)
{
    data_drop_t* data = CX_MEM_STRUCT_ALLOC(data);
    common_unpack_remote_id(_buffer, _bufferSize, _bufferPos, _outRemoteId);
    cx_binr_str(_buffer, _bufferSize, _bufferPos, data->tableName, sizeof(data->tableName));
    return data;
}

data_describe_t* common_unpack_req_describe(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId)
{
    data_describe_t* data = CX_MEM_STRUCT_ALLOC(data);
    common_unpack_remote_id(_buffer, _bufferSize, _bufferPos, _outRemoteId);

    char tableName[TABLE_NAME_LEN_MAX + 1];
    cx_binr_str(_buffer, _bufferSize, _bufferPos, tableName, sizeof(tableName));

    if (cx_str_is_empty(tableName))
    {
        data->tablesCount = 0;
        data->tables = NULL;
    }
    else
    {
        data->tablesCount = 1;
        data->tables = CX_MEM_ARR_ALLOC(data->tables, data->tablesCount);
        cx_str_copy(data->tables[0].name, sizeof(data->tables[0].name), tableName);
    }
    return data;
}

data_select_t* common_unpack_req_select(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId)
{
    data_select_t* data = CX_MEM_STRUCT_ALLOC(data);
    common_unpack_remote_id(_buffer, _bufferSize, _bufferPos, _outRemoteId);
    cx_binr_str(_buffer, _bufferSize, _bufferPos, data->tableName, sizeof(data->tableName));
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, &data->record.key);
    return data;
}

data_insert_t* common_unpack_req_insert(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId)
{
    data_insert_t* data = CX_MEM_STRUCT_ALLOC(data);
    common_unpack_remote_id(_buffer, _bufferSize, _bufferPos, _outRemoteId);
    cx_binr_str(_buffer, _bufferSize, _bufferPos, data->tableName, sizeof(data->tableName));

    common_unpack_table_record(_buffer, _bufferSize, _bufferPos, &data->record);

    return data;
}

void common_unpack_res_create(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, cx_err_t* _err)
{
    _common_unpack_res_generic(_buffer, _bufferSize, _bufferPos, _outRemoteId, _err);
}

void common_unpack_res_drop(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, cx_err_t* _err)
{
    _common_unpack_res_generic(_buffer, _bufferSize, _bufferPos, _outRemoteId, _err);
}

void common_unpack_res_describe(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, data_describe_t* _outData, cx_err_t* _err)
{
    uint16_t tablesCount = 0;

    common_unpack_remote_id(_buffer, _bufferSize, _bufferPos, _outRemoteId);

    // first packet (from the list of chunks)
    if (_outData->tablesRemaining == 0)
    {
        // get the initial tables counter
        cx_binr_uint16(_buffer, _bufferSize, _bufferPos, &tablesCount);
        _outData->tablesRemaining = tablesCount;

        // allocate more space if needed
        if (_outData->tablesRemaining != _outData->tablesCount)
        {
            if (NULL != _outData->tables) free(_outData->tables);
            _outData->tablesCount = tablesCount;
            _outData->tables = CX_MEM_ARR_ALLOC(_outData->tables, tablesCount);
        }

        // single table request
        if (1 == tablesCount)
        {
            _common_unpack_res_generic(_buffer, _bufferSize, _bufferPos, NULL, _err);
            if (ERR_NONE != _err->code)
            {
                // request failed... _err contains the reason of the failure.
                _outData->tablesRemaining = 0;
            }
        }
    }

    while (_outData->tablesRemaining > 0 && (*_bufferPos) < _bufferSize)
    {
        common_unpack_table_meta(_buffer, _bufferSize, _bufferPos, &_outData->tables[_outData->tablesCount - _outData->tablesRemaining]);
        _outData->tablesRemaining--;
    }
}

void common_unpack_res_select(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, data_select_t* _outData, cx_err_t* _err)
{
    _common_unpack_res_generic(_buffer, _bufferSize, _bufferPos, _outRemoteId, _err);

    if (ERR_NONE == _err->code)
    {
        common_unpack_table_record(_buffer, _bufferSize, _bufferPos, &_outData->record);
    }
}

void common_unpack_res_insert(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, cx_err_t* _err)
{
    _common_unpack_res_generic(_buffer, _bufferSize, _bufferPos, _outRemoteId, _err);
}

void common_unpack_table_meta(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, table_meta_t* _outTable)
{
    cx_binr_str(_buffer, _bufferSize, _bufferPos, _outTable->name, sizeof(_outTable->name));
    cx_binr_uint8(_buffer, _bufferSize, _bufferPos, &_outTable->consistency);
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, &_outTable->partitionsCount);
    cx_binr_uint32(_buffer, _bufferSize, _bufferPos, &_outTable->compactionInterval);
}

void common_unpack_table_record(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, table_record_t* _outRecord)
{
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, &_outRecord->key);

    uint16_t valueLen = cx_binr_str(_buffer, _bufferSize, _bufferPos, NULL, 0) + 1;
    _outRecord->value = malloc(valueLen);
    cx_binr_str(_buffer, _bufferSize, _bufferPos, _outRecord->value, valueLen);

    cx_binr_uint64(_buffer, _bufferSize, _bufferPos, &_outRecord->timestamp);
}

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void _common_pack_err(char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, const cx_err_t* _err)
{
    cx_binw_uint32(_buffer, _bufferSize, _bufferPos, _err->code);

    if (ERR_NONE != _err->code)
    {
        cx_binw_str(_buffer, _bufferSize, _bufferPos, _err->desc);
    }
}

static void _common_pack_res_generic(char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t _remoteId, const cx_err_t* _err)
{
    common_pack_remote_id(_buffer, _bufferSize, _bufferPos, _remoteId);
    _common_pack_err(_buffer, _bufferSize, _bufferPos, _err);
}

static void _common_unpack_err(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, cx_err_t* _err)
{
    cx_binr_uint32(_buffer, _bufferSize, _bufferPos, &_err->code);

    if (ERR_NONE != _err->code)
    {
        cx_binr_str(_buffer, _bufferSize, _bufferPos, _err->desc, sizeof(_err->desc));
    }
}

static void _common_unpack_res_generic(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId, cx_err_t* _err)
{
    common_unpack_remote_id(_buffer, _bufferSize, _bufferPos, _outRemoteId);
    _common_unpack_err(_buffer, _bufferSize, _bufferPos, _err);
}
