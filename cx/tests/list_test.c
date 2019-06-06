#include "test.h"
#include "list.h"

static cx_list_t* list = NULL;
static const uint32_t numElems = 5;
static uint32_t counter = 0;

int t_list_init()
{
    list = cx_list_init();

    bool success = true
        && NULL != list
        && numElems >= 5;

    return CUNIT_RESULT(success);
}

int t_list_cleanup()
{
    bool success = true;
    return CUNIT_RESULT(success);
}

static void _t_list_node_destroyer(void* _data)
{
    cx_list_node_t* node = (cx_list_node_t*)_data;
    CU_ASSERT((uint32_t)node->data == ++counter);
    free(node);
}

static void _t_list_func_iter(cx_list_t* _list, cx_list_node_t* _node, uint32_t _index, void* _userData)
{
    CU_ASSERT(_list == list);
    CU_ASSERT(_userData == (void*)0x999);
    CU_ASSERT((uint32_t)_node->data == ++counter);
    CU_ASSERT(_index == (counter - 1));
}

static void _t_list_should_push_single_item(bool _front)
{
    cx_list_node_t* nodeA = cx_list_node_alloc(NULL);
    
    nodeA->next = (cx_list_node_t*)0x01;
    nodeA->prev = (cx_list_node_t*)0x01;
    nodeA->data = (void*)0x999;

    if (_front)
        cx_list_push_front(list, nodeA);
    else
        cx_list_push_back(list, nodeA);

    CU_ASSERT(list->first == nodeA);
    CU_ASSERT(cx_list_peek_front(list) == nodeA);

    CU_ASSERT(list->last == nodeA);
    CU_ASSERT(cx_list_peek_back(list) == nodeA);

    CU_ASSERT(list->size == 1);
    CU_ASSERT(cx_list_size(list) == 1);

    CU_ASSERT(nodeA->next == NULL);
    CU_ASSERT(nodeA->prev == NULL);
    CU_ASSERT(nodeA->data == (void*)0x999);
}

static void _t_list_should_pop_single_item(bool _front)
{
    cx_list_node_t* nodeA = NULL;
    
    if (_front)
        nodeA = cx_list_pop_front(list);
    else
        nodeA = cx_list_pop_back(list);

    CU_ASSERT(nodeA->data == (void*)0x999);

    CU_ASSERT(list->first == NULL);
    CU_ASSERT(cx_list_peek_front(list) == NULL);

    CU_ASSERT(list->last == NULL);
    CU_ASSERT(cx_list_peek_back(list) == NULL);

    CU_ASSERT(list->size == 0);

    free(nodeA);
}

void t_list_should_push_single_item_to_the_front()
{
    _t_list_should_push_single_item(true);
}

void t_list_should_pop_single_item_from_the_front()
{
    _t_list_should_pop_single_item(true);
}

void t_list_should_push_single_item_to_the_back()
{
    _t_list_should_push_single_item(false);
}

void t_list_should_pop_single_item_from_the_back()
{
    _t_list_should_pop_single_item(false);
}

void t_list_should_push_pop_multiple_items()
{
    cx_list_node_t* node = NULL;
    
    for (uint32_t i = 0; i < numElems; i++)
    {
        node = cx_list_node_alloc(node);
        CU_ASSERT(NULL != node);
        node->data = (void*)(numElems - i);

        cx_list_push_front(list, node);
        CU_ASSERT(cx_list_peek_front(list) == node);

        CU_ASSERT(cx_list_pop_front(list) == node);

        cx_list_push_front(list, node);
        CU_ASSERT(cx_list_peek_front(list) == node);

        CU_ASSERT((i + 1) == cx_list_size(list));
    }    
}

void t_list_should_remove_in_between()
{
    cx_list_node_t* first = cx_list_peek_front(list);
    cx_list_node_t* nodeTwo = first->next;
    cx_list_node_t* nodeFour = first->next->next->next;

    CU_ASSERT((uint32_t)nodeTwo->data == 2)
    cx_list_remove(list, nodeTwo);
    CU_ASSERT((numElems - 1) == cx_list_size(list));
    free(nodeTwo);

    CU_ASSERT((uint32_t)nodeFour->data == 4)
    cx_list_remove(list, nodeFour);
    CU_ASSERT((numElems - 2) == cx_list_size(list));
    free(nodeFour);
}

void t_list_should_insert_in_between()
{
    cx_list_node_t* first = cx_list_peek_front(list);
    cx_list_node_t* nodeThree = first->next;
    cx_list_node_t* nodeFive = first->next->next;
    cx_list_node_t* nodeTwoNew = cx_list_node_alloc((void*)0x2);
    cx_list_node_t* nodeFourNew = cx_list_node_alloc((void*)0x4);
    
    cx_list_insert_before(list, nodeThree, nodeTwoNew);
    cx_list_insert_before(list, nodeFive, nodeFourNew);
    CU_ASSERT(numElems == cx_list_size(list));
}

void t_list_should_iterate_items()
{
    counter = 0;
    cx_list_foreach(list, (cx_list_func_cb)_t_list_func_iter, (void*)0x999);
    CU_ASSERT(numElems == counter);
}

void t_list_should_remove_all_items()
{
    counter = 0;
    cx_list_clear(list, (cx_destroyer_cb)_t_list_node_destroyer);
    CU_ASSERT(numElems == counter);
    CU_ASSERT(0 == cx_list_size(list));
}

void t_list_should_be_destroyed()
{
    cx_list_destroy(list, NULL);
}
