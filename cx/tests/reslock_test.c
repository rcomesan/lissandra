#include "test.h"
#include "reslock.h"

cx_reslock_t    reslockA;
cx_reslock_t    reslockB;

int t_reslock_init()
{
    list = cx_list_init();

    bool success = true
        && cx_reslock_init(&reslockA, false)
        && cx_reslock_init(&reslockB, true);

    return CUNIT_RESULT(success);
}

int t_reslock_cleanup()
{
    bool success = true;
    return CUNIT_RESULT(success);
}

void t_reslock_should_have_right_state_after_init()
{
    CU_ASSERT(!cx_reslock_is_blocked(&reslockA));
    CU_ASSERT(0 == cx_reslock_counter(&reslockA));

    CU_ASSERT(cx_reslock_is_blocked(&reslockB));    
    CU_ASSERT(0 == cx_reslock_counter(&reslockB));
}

void t_reslock_should_block_resource()
{
    cx_reslock_block(&reslockA);
    CU_ASSERT(cx_reslock_is_blocked(&reslockA));
}

void t_reslock_should_not_grant_resource_when_blocked()
{
    CU_ASSERT(!cx_reslock_avail_guard_begin(&reslockA));
    CU_ASSERT(!cx_reslock_avail_guard_begin(&reslockB));
}

void t_reslock_should_unblock_resource()
{
    cx_reslock_unblock(&reslockA);
    cx_reslock_unblock(&reslockB);
    CU_ASSERT(!cx_reslock_is_blocked(&reslockA));
    CU_ASSERT(!cx_reslock_is_blocked(&reslockB));

    CU_ASSERT_DOUBLE_EQUAL(cx_reslock_blocked_time(&reslockA), 0, 0.5);
    CU_ASSERT_DOUBLE_EQUAL(cx_reslock_blocked_time(&reslockB), 0, 0.5);
}

void t_reslock_should_grant_resource_when_unblocked()
{
    CU_ASSERT(cx_reslock_avail_guard_begin(&reslockA));
    CU_ASSERT(cx_reslock_avail_guard_begin(&reslockA));
    CU_ASSERT(cx_reslock_avail_guard_begin(&reslockA));

    CU_ASSERT(3 == cx_reslock_counter(&reslockA));

    cx_reslock_avail_guard_end(&reslockA);
    CU_ASSERT(2 == cx_reslock_counter(&reslockA));

    cx_reslock_avail_guard_end(&reslockA);
    CU_ASSERT(1 == cx_reslock_counter(&reslockA));

    cx_reslock_avail_guard_end(&reslockA);
    CU_ASSERT(0 == cx_reslock_counter(&reslockA));
}

void t_reslock_should_be_destroyed()
{
    cx_reslock_destroy(&reslockA);
    cx_reslock_destroy(&reslockB);
}