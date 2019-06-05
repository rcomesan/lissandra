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

    // run tests
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    CU_cleanup_registry();
    return CU_get_error();
}