#include "test.h"
#include "halloc.h"
#include "mem.h"

static cx_handle_alloc_t*   halloc = NULL;
static const uint32_t       hallocCapacity = 5;

int t_halloc_init()
{
    halloc = cx_halloc_init(hallocCapacity);

    bool success = true
        && NULL != halloc
        && hallocCapacity >= 5;

    return CUNIT_RESULT(success);
}

int t_halloc_cleanup()
{
    bool success = true;
    return CUNIT_RESULT(success);
}

void t_halloc_should_alloc_handles()
{
    uint16_t handle = INVALID_HANDLE;

    CU_ASSERT(cx_handle_capacity(halloc));

    for (uint32_t i = 0; i < hallocCapacity; i++)
    {
        handle = cx_handle_alloc(halloc);
        CU_ASSERT(INVALID_HANDLE != handle);
        CU_ASSERT(i + 1 == cx_handle_count(halloc));

        cx_handle_free(halloc, handle);
        CU_ASSERT(i == cx_handle_count(halloc));

        handle = cx_handle_alloc(halloc);
        CU_ASSERT(cx_handle_is_valid(halloc, handle));
    }
}

void t_halloc_should_retrieve_handles_by_index()
{
    uint16_t handle = INVALID_HANDLE;

    for (uint32_t i = 0; i < cx_handle_count(halloc); i++)
    {
        handle = cx_handle_at(halloc, i);
        CU_ASSERT(INVALID_HANDLE != handle);
        
        CU_ASSERT(i == handle);
        CU_ASSERT(cx_handle_is_valid(halloc, handle));
    }
}

void t_halloc_should_alloc_handles_with_key()
{
    uint16_t handle = INVALID_HANDLE;
    const int32_t key[] = { 0, 66, 127312, INT32_MAX, INT32_MIN + 1 };
    
    CU_ASSERT(INVALID_HANDLE == cx_handle_alloc_key(halloc, INT32_MIN));

    for (uint32_t i = 0; i < CX_ARR_SIZE(key); i++)
    {
        cx_handle_free(halloc, hallocCapacity - 1 - i);
        handle = cx_handle_alloc_key(halloc, key[i]);

        CU_ASSERT(INVALID_HANDLE != handle);
        CU_ASSERT(handle == cx_handle_get(halloc, key[i]));
        CU_ASSERT(cx_handle_contains(halloc, key[i]));
        CU_ASSERT(cx_handle_is_valid(halloc, handle));

        handle = cx_handle_alloc_key(halloc, key[i]);
        CU_ASSERT(INVALID_HANDLE == handle);
    }
    
    CU_ASSERT(cx_handle_count(halloc) == cx_handle_capacity(halloc));
}

void t_halloc_should_remove_all_handles()
{
    cx_halloc_reset(halloc);
    CU_ASSERT(0 == cx_handle_count(halloc));
}

void t_halloc_should_destroy_halloc()
{
    CU_ASSERT(INVALID_HANDLE != cx_handle_alloc(halloc));
    CU_ASSERT(INVALID_HANDLE != cx_handle_alloc_key(halloc, 0));
    cx_halloc_destroy(halloc);
}