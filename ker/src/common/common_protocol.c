#include <ker/common_protocol.h>

#include <cx/binr.h>
#include <cx/binw.h>
#include <cx/mem.h>
#include <cx/str.h>

/****************************************************************************************
 ***  COMMON MESSAGE PACKERS
 ***************************************************************************************/

uint32_t common_pack_req_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    cx_binw_uint8(_buffer, _size, &pos, _consistency);
    cx_binw_uint16(_buffer, _size, &pos, _numPartitions);
    cx_binw_uint32(_buffer, _size, &pos, _compactionInterval);
    return pos;
}

uint32_t common_pack_req_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    return pos;
}

uint32_t common_pack_req_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, (NULL != _tableName) ? _tableName : "");
    return pos;
}

uint32_t common_pack_req_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    cx_binw_uint16(_buffer, _size, &pos, _key);
    return pos;
}

uint32_t common_pack_req_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint32_t _timestamp)
{
    uint32_t pos = 0;
    cx_binw_uint16(_buffer, _size, &pos, _remoteId);
    cx_binw_str(_buffer, _size, &pos, _tableName);
    cx_binw_uint16(_buffer, _size, &pos, _key);
    cx_binw_str(_buffer, _size, &pos, _value);
    cx_binw_uint32(_buffer, _size, &pos, _timestamp);
    return pos;
}

/****************************************************************************************
 ***  COMMON MESSAGE UNPACKERS
 ***************************************************************************************/

data_create_t* common_unpack_req_create(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId)
{
    data_create_t* data = CX_MEM_STRUCT_ALLOC(data);
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, _outRemoteId);
    cx_binr_str(_buffer, _bufferSize, _bufferPos, data->tableName, sizeof(data->tableName));
    cx_binr_uint8(_buffer, _bufferSize, _bufferPos, &data->consistency);
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, &data->numPartitions);
    cx_binr_uint32(_buffer, _bufferSize, _bufferPos, &data->compactionInterval);
    return data;
}

data_drop_t* common_unpack_req_drop(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId)
{
    data_drop_t* data = CX_MEM_STRUCT_ALLOC(data);
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, _outRemoteId);
    cx_binr_str(_buffer, _bufferSize, _bufferPos, data->tableName, sizeof(data->tableName));
    return data;
}

data_describe_t* common_unpack_req_describe(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId)
{
    data_describe_t* data = CX_MEM_STRUCT_ALLOC(data);
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, _outRemoteId);

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
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, _outRemoteId);
    cx_binr_str(_buffer, _bufferSize, _bufferPos, data->tableName, sizeof(data->tableName));
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, &data->record.key);
    return data;
}

data_insert_t* common_unpack_req_insert(const char* _buffer, uint16_t _bufferSize, uint32_t* _bufferPos, uint16_t* _outRemoteId)
{
    data_insert_t* data = CX_MEM_STRUCT_ALLOC(data);
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, _outRemoteId);
    cx_binr_str(_buffer, _bufferSize, _bufferPos, data->tableName, sizeof(data->tableName));
    cx_binr_uint16(_buffer, _bufferSize, _bufferPos, &data->record.key);

    uint16_t valueLen = cx_binr_str(_buffer, _bufferSize, _bufferPos, NULL, 0) + 1;
    data->record.value = malloc(valueLen);
    cx_binr_str(_buffer, _bufferSize, _bufferPos, data->record.value, valueLen);

    cx_binr_uint32(_buffer, _bufferSize, _bufferPos, &data->record.timestamp);
    return data;
}