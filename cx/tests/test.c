#include "list_test.c"

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

    // run tests
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();
    return CU_get_error();
}