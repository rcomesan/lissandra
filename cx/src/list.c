#include "list.h"
#include "mem.h"

/****************************************************************************************
***  PRIVATE DECLARATIONS
***************************************************************************************/

static void             _list_node_destroyer(cx_list_t* _list, cx_list_node_t* _node, uint32_t _index, void* _dataDestroyer);

static cx_list_node_t*  _list_node_new(cx_list_node_t* _prev, cx_list_node_t* _next, void* _data);

static void             _list_node_insert_before(cx_list_t* _list, cx_list_node_t* _pos, cx_list_node_t* _node);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

cx_list_t* cx_list_init()
{
    cx_list_t* list = CX_MEM_STRUCT_ALLOC(list);
    return list;
}

void cx_list_destroy(cx_list_t* _list, cx_destroyer_cb _nodeDestroyer)
{
    if (NULL == _list) return;
    cx_list_clear(_list, _nodeDestroyer);
    free(_list);
}

void cx_list_clear(cx_list_t* _list, cx_destroyer_cb _nodeDestroyer)
{
    CX_CHECK_NOT_NULL(_list);

    if (NULL != _nodeDestroyer)
    {
        cx_list_foreach(_list, (cx_list_func_cb)_list_node_destroyer, _nodeDestroyer);
    }

    _list->size = 0;
    _list->first = NULL;
    _list->last = NULL;
}

uint32_t cx_list_size(cx_list_t* _list)
{
    CX_CHECK_NOT_NULL(_list);
    return _list->size;
}

void cx_list_remove(cx_list_t* _list, cx_list_node_t* _node)
{
    if (NULL == _node)
    {
        CX_WARN(CX_ALW, "you're trying to remove a NULL node!");
        return;
    }

    if (1 == _list->size)
    {
        _list->first = NULL;
        _list->last = NULL;
    }
    else if (_list->first == _node)
    {
        _node->next->prev = NULL;
        _list->first = _node->next;
    }
    else if (_list->last == _node)
    {
        _node->prev->next = NULL;
        _list->last = _node->prev;
    }
    else
    {
        _node->prev->next = _node->next;
        _node->next->prev = _node->prev;
    }

    _list->size--;
}

cx_list_node_t* cx_list_node_alloc(void* _data)
{
    return _list_node_new(NULL, NULL, _data);
}

void cx_list_insert_before(cx_list_t* _list, cx_list_node_t* _pos, cx_list_node_t* _node)
{
    _list_node_insert_before(_list, _pos, _node);
}

void cx_list_push_front(cx_list_t* _list, cx_list_node_t* _node)
{
    _list_node_insert_before(_list, _list->first, _node);
}

void cx_list_push_back(cx_list_t* _list, cx_list_node_t* _node)
{
    _list_node_insert_before(_list, NULL, _node);
}

cx_list_node_t* cx_list_pop_front(cx_list_t* _list)
{
    CX_CHECK_NOT_NULL(_list);
    cx_list_node_t* node = _list->first;
    cx_list_remove(_list, node);
    return node;
}

cx_list_node_t* cx_list_pop_back(cx_list_t* _list)
{
    CX_CHECK_NOT_NULL(_list);
    cx_list_node_t* node = _list->last;
    cx_list_remove(_list, node);
    return node;
}

cx_list_node_t* cx_list_peek_front(cx_list_t* _list)
{
    CX_CHECK_NOT_NULL(_list);
    return _list->first;
}

cx_list_node_t* cx_list_peek_back(cx_list_t* _list)
{
    CX_CHECK_NOT_NULL(_list);
    return _list->last;
}

void cx_list_foreach(cx_list_t* _list, cx_list_func_cb _func, void* _userData)
{
    CX_CHECK_NOT_NULL(_list);

    uint32_t index = 0;
    cx_list_node_t* node = _list->first;
    cx_list_node_t* next = NULL;

    while (node)
    {
        next = node->next;
        _func(_list, node, index++, _userData);
        node = next;
    }
}

cx_list_node_t* cx_list_get(cx_list_t* _list, uint32_t _index)
{
    uint32_t count = 0;
    cx_list_node_t* node = _list->first;

    while (count < _index)
    {
        if (NULL == node) return NULL;
        node = node->next;
        count++;
    }

    return node;
}

/****************************************************************************************
***  PRIVATE FUNCTIONS
***************************************************************************************/

static void _list_node_destroyer(cx_list_t* _list, cx_list_node_t* _node, uint32_t _index, void* _nodeDestroyer)
{
    cx_destroyer_cb nodeDestroyer = (cx_destroyer_cb)_nodeDestroyer;
    nodeDestroyer(_node);
}

static cx_list_node_t* _list_node_new(cx_list_node_t* _prev, cx_list_node_t* _next, void* _data)
{
    cx_list_node_t* newNode = CX_MEM_STRUCT_ALLOC(newNode);
    newNode->prev = _prev;
    newNode->next = _next;
    newNode->data = _data;
    return newNode;
}

static void _list_node_insert_before(cx_list_t* _list, cx_list_node_t* _pos, cx_list_node_t* _node)
{
    bool setLast = false;
    bool setFirst = false;

    if (NULL == _node)
    {
        CX_WARN(CX_ALW, "you're trying to insert a NULL node!");
        return;
    }

    if (0 == _list->size)           // empty list
    {
        _node->prev = NULL;
        _node->next = NULL;
        setFirst = true;
        setLast = true;
    }
    else if (_list->first == _pos)  // insert first
    {
        _node->prev = NULL;
        _node->next = _list->first;
        setFirst = true;
        setLast = (0 == _list->size);
    }
    else if (NULL == _pos)          // insert last
    {
        _node->prev = _list->last;
        _node->next = NULL;
        setFirst = (0 == _list->size);
        setLast = true;
    }
    else                            // insert in between
    {
        _node->prev = _pos->prev;
        _node->next = _pos;
        _pos->prev->next = _node;
        _pos->prev = _node;
    }

    if (setFirst)
    {
        if (_list->first) _list->first->prev = _node;
        _list->first = _node;
    }

    if (setLast)
    {
        if (_list->last) _list->last->next = _node;
        _list->last = _node;
    }

    _list->size++;
}
