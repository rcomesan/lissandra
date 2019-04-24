#ifndef CX_SORT_H_
#define CX_SORT_H_

#include <stdint.h>

typedef int32_t(*cx_sort_comp_cb)(const void* _a, const void* _b);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void        cx_sort_quick(void* _data, uint32_t _size, uint32_t _num, cx_sort_comp_cb _comp);

uint32_t    cx_sort_find(void* _data, uint32_t _size, uint32_t _num, const void* _key, cx_sort_comp_cb _comp);

#endif // CX_SORT_H_
