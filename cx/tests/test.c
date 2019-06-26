#include "binrw_test.c"
#include "list_test.c"
#include "halloc_test.c"
#include "reslock_test.c"
#include "sort_test.c"

#include <CUnit/Basic.h>

int main(int _argc, char** _argv)
{
    CU_pSuite suite = NULL;

    // init CUnit registry
    if (CUE_SUCCESS != CU_initialize_registry())
    {
        return CU_get_error();
    }

    // +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

    suite = CU_add_suite("binrw_test.c", t_binrw_init, t_binrw_cleanup);
    if (NULL == suite
        || (NULL == CU_add_test(suite, "t_binrw_should_rw_int32()", t_binrw_should_rw_int32))
        || (NULL == CU_add_test(suite, "t_binrw_should_rw_uint32()", t_binrw_should_rw_uint32))
        || (NULL == CU_add_test(suite, "t_binrw_should_rw_int32()", t_binrw_should_rw_int16))
        || (NULL == CU_add_test(suite, "t_binrw_should_rw_uint32()", t_binrw_should_rw_uint16))
        || (NULL == CU_add_test(suite, "t_binrw_should_rw_int32()", t_binrw_should_rw_int8))
        || (NULL == CU_add_test(suite, "t_binrw_should_rw_uint32()", t_binrw_should_rw_uint8))
        || (NULL == CU_add_test(suite, "t_binrw_should_rw_float()", t_binrw_should_rw_float))
        || (NULL == CU_add_test(suite, "t_binrw_should_rw_double()", t_binrw_should_rw_double))
        || (NULL == CU_add_test(suite, "t_binrw_should_rw_bool()", t_binrw_should_rw_bool))
        || (NULL == CU_add_test(suite, "t_binrw_should_rw_str()", t_binrw_should_rw_str))
        || (NULL == CU_add_test(suite, "t_binrw_should_rw_str_when_empty()", t_binrw_should_rw_str_when_empty))
        )
    {
        CU_cleanup_registry();
        return CU_get_error();
    }

    // +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

    suite = CU_add_suite("halloc_test.c", t_halloc_init, t_halloc_cleanup);
    if (NULL == suite
        || (NULL == CU_add_test(suite, "t_halloc_should_alloc_handles()", t_halloc_should_alloc_handles))
        || (NULL == CU_add_test(suite, "t_halloc_should_retrieve_handles_by_index()", t_halloc_should_retrieve_handles_by_index))
        || (NULL == CU_add_test(suite, "t_halloc_should_alloc_handles_with_key()", t_halloc_should_alloc_handles_with_key))
        || (NULL == CU_add_test(suite, "t_halloc_should_remove_all_handles()", t_halloc_should_remove_all_handles))
        || (NULL == CU_add_test(suite, "t_halloc_should_destroy_halloc()", t_halloc_should_destroy_halloc))
        )
    {
        CU_cleanup_registry();
        return CU_get_error();
    }

    // +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

    suite = CU_add_suite("list_test.c", t_list_init, t_list_cleanup);
    if (NULL == suite
        || (NULL == CU_add_test(suite, "t_list_should_push_single_item_to_the_front()", t_list_should_push_single_item_to_the_front))
        || (NULL == CU_add_test(suite, "t_list_should_pop_single_item_from_the_front()", t_list_should_pop_single_item_from_the_front))
        || (NULL == CU_add_test(suite, "t_list_should_push_single_item_to_the_back()", t_list_should_push_single_item_to_the_back))
        || (NULL == CU_add_test(suite, "t_list_should_pop_single_item_from_the_back()", t_list_should_pop_single_item_from_the_back))
        || (NULL == CU_add_test(suite, "t_list_should_push_pop_multiple_items()", t_list_should_push_pop_multiple_items))
        || (NULL == CU_add_test(suite, "t_list_should_get_items_by_index()", t_list_should_get_items_by_index))
        || (NULL == CU_add_test(suite, "t_list_should_remove_in_between()", t_list_should_remove_in_between))
        || (NULL == CU_add_test(suite, "t_list_should_insert_in_between()", t_list_should_insert_in_between))
        || (NULL == CU_add_test(suite, "t_list_should_iterate_items()", t_list_should_iterate_items))
        || (NULL == CU_add_test(suite, "t_list_should_remove_all_items()", t_list_should_remove_all_items))
        || (NULL == CU_add_test(suite, "t_list_should_be_destroyed()", t_list_should_be_destroyed))
    )
    {
        CU_cleanup_registry();
        return CU_get_error();
    }

    // +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

    suite = CU_add_suite("reslock_test.c", t_reslock_init, t_reslock_cleanup);
    if (NULL == suite
        || (NULL == CU_add_test(suite, "t_reslock_should_have_right_state_after_init()", t_reslock_should_have_right_state_after_init))
        || (NULL == CU_add_test(suite, "t_reslock_should_block_resource()", t_reslock_should_block_resource))
        || (NULL == CU_add_test(suite, "t_reslock_should_not_grant_resource_when_blocked()", t_reslock_should_not_grant_resource_when_blocked))
        || (NULL == CU_add_test(suite, "t_reslock_should_unblock_resource()", t_reslock_should_unblock_resource))
        || (NULL == CU_add_test(suite, "t_reslock_should_grant_resource_when_unblocked()", t_reslock_should_grant_resource_when_unblocked))
        || (NULL == CU_add_test(suite, "t_reslock_should_be_destroyed()", t_reslock_should_be_destroyed))
        )
    {
        CU_cleanup_registry();
        return CU_get_error();
    }

    // +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

    suite = CU_add_suite("sort_test.c", t_sort_init, t_sort_cleanup);
    if (NULL == suite
        || (NULL == CU_add_test(suite, "t_sort_should_sort_when_empty()", t_sort_should_sort_when_empty))
        || (NULL == CU_add_test(suite, "t_sort_should_sort_when_single_element()", t_sort_should_sort_when_single_element))
        || (NULL == CU_add_test(suite, "t_sort_should_sort_when_two_identical_elements()", t_sort_should_sort_when_two_identical_elements))
        || (NULL == CU_add_test(suite, "t_sort_should_sort_when_three_identical_elements()", t_sort_should_sort_when_three_identical_elements))
        || (NULL == CU_add_test(suite, "t_sort_should_sort_in_ascending_order()", t_sort_should_sort_in_ascending_order))
        || (NULL == CU_add_test(suite, "t_sort_should_find_element()", t_sort_should_find_element))
        || (NULL == CU_add_test(suite, "t_sort_should_find_first_occurrence_of_element()", t_sort_should_find_first_occurrence_of_element))
        || (NULL == CU_add_test(suite, "t_sort_should_find_insert_position_for_missing_element()", t_sort_should_find_insert_position_for_missing_element))
        || (NULL == CU_add_test(suite, "t_sort_should_remove_duplicates()", t_sort_should_remove_duplicates))
        )
    {
        CU_cleanup_registry();
        return CU_get_error();
    }

    // +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

    // run tests
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();
    return CU_get_error();
}