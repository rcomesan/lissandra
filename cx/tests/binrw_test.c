#include "test.h"
#include "binr.h"
#include "binw.h"

static char     binrwBuff[1024];
static uint16_t binrwSize = sizeof(binrwBuff);
static uint32_t binrwPos;

int t_binrw_init()
{
    bool success = true;
    return CUNIT_RESULT(success);
}

int t_binrw_cleanup()
{
    bool success = true;
    return CUNIT_RESULT(success);
}

static void _t_binrw_should_rw_str(const char* _value)
{
    uint32_t  origPos = binrwPos;
    uint32_t  valueLen = strlen(_value);
    char*     bufferValue = NULL;

    cx_binw_str(binrwBuff, binrwSize, &binrwPos, _value);
    CU_ASSERT(binrwPos == origPos + sizeof(uint16_t) + valueLen);

    uint32_t origPosPrev = origPos;
    CU_ASSERT(valueLen == cx_binr_str(binrwBuff, binrwSize, &origPos, NULL, 0));
    CU_ASSERT(origPos == origPosPrev);

    bufferValue = malloc(valueLen + 1);

    CU_ASSERT(valueLen == cx_binr_str(binrwBuff, binrwSize, &origPos, bufferValue, valueLen + 1));
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(0 == strncmp(_value, bufferValue, valueLen));

    free(bufferValue);
}

void t_binrw_should_rw_int64()
{
    uint32_t  origPos = binrwPos;
    int64_t   value = INT64_MAX;
    int64_t   bufferValue;

    cx_binw_int64(binrwBuff, binrwSize, &binrwPos, value);
    CU_ASSERT(binrwPos == origPos + sizeof(value));

    cx_binr_int64(binrwBuff, binrwSize, &origPos, &bufferValue);
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(value == bufferValue);
}

void t_binrw_should_rw_uint64()
{
    uint32_t  origPos = binrwPos;
    uint64_t  value = UINT64_MAX;
    uint64_t  bufferValue;

    cx_binw_uint64(binrwBuff, binrwSize, &binrwPos, value);
    CU_ASSERT(binrwPos == origPos + sizeof(value));

    cx_binr_uint64(binrwBuff, binrwSize, &origPos, &bufferValue);
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(value == bufferValue);
}

void t_binrw_should_rw_int32()
{
    uint32_t  origPos = binrwPos;
    int32_t   value = INT32_MAX;
    int32_t   bufferValue;

    cx_binw_int32(binrwBuff, binrwSize, &binrwPos, value);
    CU_ASSERT(binrwPos == origPos + sizeof(value));

    cx_binr_int32(binrwBuff, binrwSize, &origPos, &bufferValue);
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(value == bufferValue);
}

void t_binrw_should_rw_uint32()
{
    uint32_t  origPos = binrwPos;
    uint32_t  value = UINT32_MAX;
    uint32_t  bufferValue;

    cx_binw_uint32(binrwBuff, binrwSize, &binrwPos, value);
    CU_ASSERT(binrwPos == origPos + sizeof(value));

    cx_binr_uint32(binrwBuff, binrwSize, &origPos, &bufferValue);
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(value == bufferValue);
}

void t_binrw_should_rw_int16()
{
    uint32_t  origPos = binrwPos;
    int16_t   value = INT16_MAX;
    int16_t   bufferValue;

    cx_binw_int16(binrwBuff, binrwSize, &binrwPos, value);
    CU_ASSERT(binrwPos == origPos + sizeof(value));

    cx_binr_int16(binrwBuff, binrwSize, &origPos, &bufferValue);
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(value == bufferValue);
}

void t_binrw_should_rw_uint16()
{
    uint32_t  origPos = binrwPos;
    uint16_t  value = UINT16_MAX;
    uint16_t  bufferValue;

    cx_binw_uint16(binrwBuff, binrwSize, &binrwPos, value);
    CU_ASSERT(binrwPos == origPos + sizeof(value));

    cx_binr_uint16(binrwBuff, binrwSize, &origPos, &bufferValue);
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(value == bufferValue);
}

void t_binrw_should_rw_int8()
{
    uint32_t  origPos = binrwPos;
    int8_t    value = INT8_MAX;
    int8_t    bufferValue;

    cx_binw_int8(binrwBuff, binrwSize, &binrwPos, value);
    CU_ASSERT(binrwPos == origPos + sizeof(value));

    cx_binr_int8(binrwBuff, binrwSize, &origPos, &bufferValue);
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(value == bufferValue);
}

void t_binrw_should_rw_uint8()
{
    uint32_t  origPos = binrwPos;
    uint8_t   value = UINT8_MAX;
    uint8_t   bufferValue;

    cx_binw_uint8(binrwBuff, binrwSize, &binrwPos, value);
    CU_ASSERT(binrwPos == origPos + sizeof(value));

    cx_binr_uint8(binrwBuff, binrwSize, &origPos, &bufferValue);
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(value == bufferValue);
}

void t_binrw_should_rw_float()
{
    uint32_t  origPos = binrwPos;
    float     value = 3284923.29382f;
    float     bufferValue;

    cx_binw_float(binrwBuff, binrwSize, &binrwPos, value);
    CU_ASSERT(binrwPos == origPos + sizeof(value));

    cx_binr_float(binrwBuff, binrwSize, &origPos, &bufferValue);
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(value == bufferValue);
}

void t_binrw_should_rw_double()
{
    uint32_t  origPos = binrwPos;
    double    value = 213889123891238129312.3912038213982;
    double    bufferValue;

    cx_binw_double(binrwBuff, binrwSize, &binrwPos, value);
    CU_ASSERT(binrwPos == origPos + sizeof(value));

    cx_binr_double(binrwBuff, binrwSize, &origPos, &bufferValue);
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(value == bufferValue);
}

void t_binrw_should_rw_bool()
{
    uint32_t  origPos = binrwPos;
    bool      value = true;
    bool      bufferValue;

    cx_binw_bool(binrwBuff, binrwSize, &binrwPos, value);
    CU_ASSERT(binrwPos == origPos + sizeof(value));

    cx_binr_bool(binrwBuff, binrwSize, &origPos, &bufferValue);
    CU_ASSERT(binrwPos == origPos);
    CU_ASSERT(value == bufferValue);
}

void t_binrw_should_rw_str()
{
    _t_binrw_should_rw_str("cx-tests");
}

void t_binrw_should_rw_str_when_empty()
{
    _t_binrw_should_rw_str("");
}