#include <ker/common_protocol.h>
#include <mem/mem_protocol.h>
#include <lfs/lfs_protocol.h>
#include <ker/defines.h>
#include <ker/taskman.h>

#include <cx/binr.h>
#include <cx/binw.h>
#include <cx/halloc.h>
#include <cx/mem.h>
#include <cx/str.h>

/****************************************************************************************
 ***  MESSAGE HANDLERS
 ***************************************************************************************/

#ifdef MEM

#include "../mem.h"

void mem_handle_create(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_CREATE);
    {
        task->data = common_unpack_create(_buffer, _bufferSize, &bufferPos, &task->remoteId);
    }
    REQ_END;
}

void mem_handle_drop(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_DROP);
    {
        task->data = common_unpack_drop(_buffer, _bufferSize, &bufferPos, &task->remoteId);
    }
    REQ_END;
}

void mem_handle_describe(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_DESCRIBE);
    {
        task->data = common_unpack_describe(_buffer, _bufferSize, &bufferPos, &task->remoteId);
    }
    REQ_END;
}

void mem_handle_select(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_SELECT);
    {
        task->data = common_unpack_select(_buffer, _bufferSize, &bufferPos, &task->remoteId);
    }
    REQ_END;
}

void mem_handle_insert(const cx_net_common_t* _common, void* _userData, const char* _buffer, uint16_t _bufferSize)
{
    REQ_BEGIN(TASK_WT_INSERT);
    {
        task->data = common_unpack_insert(_buffer, _bufferSize, &bufferPos, &task->remoteId);
    }
    REQ_END;
}

#endif // MEM

/****************************************************************************************
 ***  MESSAGE PACKERS
 ***************************************************************************************/

uint32_t mem_pack_create(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint8_t _consistency, uint16_t _numPartitions, uint32_t _compactionInterval)
{
    return common_pack_create(_buffer, _size, _remoteId, _tableName, _consistency, _numPartitions, _compactionInterval);
}

uint32_t mem_pack_drop(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    return common_pack_drop(_buffer, _size, _remoteId, _tableName);
}

uint32_t mem_pack_describe(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName)
{
    return common_pack_describe(_buffer, _size, _remoteId, _tableName);
}

uint32_t mem_pack_select(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key)
{
    return common_pack_select(_buffer, _size, _remoteId, _tableName, _key);
}

uint32_t mem_pack_insert(char* _buffer, uint16_t _size, uint16_t _remoteId, const char* _tableName, uint16_t _key, const char* _value, uint32_t _timestamp)
{
    return common_pack_insert(_buffer, _size, _remoteId, _tableName, _key, _value, _timestamp);
}
