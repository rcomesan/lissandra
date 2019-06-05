#ifndef CX_LIST_H_
#define CX_LIST_H_

#include "cx.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct cx_list_node_t cx_list_node_t;
typedef struct cx_list_t cx_list_t;

struct cx_list_node_t
{
    void*               data;
    cx_list_node_t*     next;
    cx_list_node_t*     prev;
};

struct cx_list_t
{
    cx_list_node_t*     first;
    cx_list_node_t*     last;
    uint32_t            size;
};

typedef void(*cx_list_func_cb)(cx_list_t* _list, cx_list_node_t* _node, uint32_t _index, void* _userData);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_list_t*              cx_list_init();

void                    cx_list_destroy(cx_list_t* _list, cx_destroyer_cb _cb);

void                    cx_list_clear(cx_list_t* _list, cx_destroyer_cb _cb);

uint32_t                cx_list_size(cx_list_t* _list);

void                    cx_list_remove(cx_list_t* _list, cx_list_node_t* _node);

cx_list_node_t*         cx_list_node_alloc(void* _data);

void                    cx_list_insert_before(cx_list_t* _list, cx_list_node_t* _pos, cx_list_node_t* _node);

void                    cx_list_push_front(cx_list_t* _list, cx_list_node_t* _node);

void                    cx_list_push_back(cx_list_t* _list, cx_list_node_t* _node);

cx_list_node_t*         cx_list_pop_front(cx_list_t* _list);

cx_list_node_t*         cx_list_pop_back(cx_list_t* _list);

cx_list_node_t*         cx_list_peek_front(cx_list_t* _list);

cx_list_node_t*         cx_list_peek_back(cx_list_t* _list);

void                    cx_list_foreach(cx_list_t* _list, cx_list_func_cb _func, void* _userData);

#endif // CX_LIST_H_