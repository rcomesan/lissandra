#include "test.h"
#include "sort.h"

uint32_t sortCounter = 0;
int32_t  sortNumbers[] = {
    88,     88,     525,    88,
    6312,   525,    233,    1,
    1,      666,    666,    0,
    100,    525,    0,      88
};

int32_t _sort_comp_asc(void* _a, void* _b)
{
    return (*(int32_t*)(_a)) - (*(int32_t*)(_b));
}

int32_t _sort_comp_desc(void* _a, void* _b)
{
    return (*(int32_t*)(_b)) - (*(int32_t*)(_a));
}

int t_sort_init()
{
    bool success = true;
    return CUNIT_RESULT(success);
}

int t_sort_cleanup()
{
    bool success = true;
    return CUNIT_RESULT(success);
}

static void _t_sort_elem_destroyer(void* _data)
{
    sortCounter++;
}

void t_sort_should_sort_when_empty()
{
    int32_t numbers[] = {};
    cx_sort_quick(&numbers, sizeof(int32_t), CX_ARR_SIZE(numbers),
        (cx_sort_comp_cb)_sort_comp_asc, NULL);
}

void t_sort_should_sort_when_single_element()
{
    int32_t numbers[] = { 1 };
    cx_sort_quick(&numbers, sizeof(int32_t), CX_ARR_SIZE(numbers),
        (cx_sort_comp_cb)_sort_comp_asc, NULL);
}

void t_sort_should_sort_when_two_identical_elements()
{
    int32_t numbers[] = { 1, 1 };
    cx_sort_quick(&numbers, sizeof(int32_t), CX_ARR_SIZE(numbers),
        (cx_sort_comp_cb)_sort_comp_asc, NULL);
}

void t_sort_should_sort_when_three_identical_elements()
{
    int32_t numbers[] = { 1, 1, 1 };
    cx_sort_quick(&numbers, sizeof(int32_t), CX_ARR_SIZE(numbers),
        (cx_sort_comp_cb)_sort_comp_asc, NULL);
}

void t_sort_should_sort_in_ascending_order()
{
    cx_sort_quick(&sortNumbers, sizeof(sortNumbers[0]), CX_ARR_SIZE(sortNumbers),
        (cx_sort_comp_cb)_sort_comp_asc, NULL);
    
    for (uint32_t i = 1; i < CX_ARR_SIZE(sortNumbers); i++)
    {
        CU_ASSERT(sortNumbers[i - 1] <= sortNumbers[i]);
    }
}

void t_sort_should_find_element()
{
    int32_t number = 666;
    uint32_t index = cx_sort_find(&sortNumbers, sizeof(sortNumbers[0]), 
        CX_ARR_SIZE(sortNumbers), &number, false, (cx_sort_comp_cb)_sort_comp_asc, NULL);

    CU_ASSERT(sortNumbers[index] == number);
}

void t_sort_should_find_first_occurrence_of_element()
{
    int32_t numbers[] = { 525, 666 };
    uint32_t index = 0;

    int32_t zero = 0;
    index = cx_sort_find(&sortNumbers, sizeof(sortNumbers[0]), 
        CX_ARR_SIZE(sortNumbers), &zero, true, (cx_sort_comp_cb)_sort_comp_asc, NULL);
    CU_ASSERT(0 == index);

    for (uint32_t i = 0; i < CX_ARR_SIZE(numbers); i++)
    {
        index = cx_sort_find(&sortNumbers, sizeof(sortNumbers[0]), 
            CX_ARR_SIZE(sortNumbers), &numbers[i], true, (cx_sort_comp_cb)_sort_comp_asc, NULL);

        CU_ASSERT(numbers[i] != sortNumbers[index - 1]);
        CU_ASSERT(numbers[i] == sortNumbers[index]);
        CU_ASSERT(numbers[i] == sortNumbers[index + 1]);
    }
}

void t_sort_should_find_insert_position_for_missing_element()
{
    int32_t number = 3;
    int32_t index = cx_sort_find(&sortNumbers, sizeof(sortNumbers[0]), 
        CX_ARR_SIZE(sortNumbers), &number, true, (cx_sort_comp_cb)_sort_comp_asc, NULL);

    CU_ASSERT(0 > index);

    index = abs(index + 1);
    CU_ASSERT(4 == index);

    CU_ASSERT(number > sortNumbers[index - 1]);
    CU_ASSERT(number < sortNumbers[index]);
}

void t_sort_should_remove_duplicates()
{
    sortCounter = 0;
    uint32_t numElementsPrev = CX_ARR_SIZE(sortNumbers);

    uint32_t numElements = cx_sort_uniquify(&sortNumbers, sizeof(sortNumbers[0]), 
        CX_ARR_SIZE(sortNumbers), (cx_sort_comp_cb)_sort_comp_asc, NULL,
        (cx_destroyer_cb)_t_sort_elem_destroyer);

    CU_ASSERT(8 == numElements);
    CU_ASSERT(sortCounter == (numElementsPrev - numElements));

    for (uint32_t i = 1; i < numElements; i++)
    {
        CU_ASSERT(sortNumbers[i - 1] <= sortNumbers[i]);
    }
}