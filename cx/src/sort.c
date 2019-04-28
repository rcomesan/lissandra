#include "cx.h"
#include "sort.h"

#include <string.h>

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void _cx_sort(void* _data, uint32_t _size, uint32_t _num, cx_sort_comp_cb _comp, void* _userData, void* _pivot, void* _temp);

static void _cx_swap(void* _a, void* _b, void* _temp, uint32_t _size);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void cx_sort_quick(void* _data, uint32_t _size, uint32_t _num, cx_sort_comp_cb _comp, void* _userData)
{
    // sorts the _data array of _num elements of _size bytes each using quicksort algorithm.
    // comparisons are made using _comp callback which should return:
    // a value lesss than zero      if _a should be placed to the left of _b.
    // a value greater than zero    if _a should be placed to the right of _b.
    // a value equal to zero        if _a and _b are equal.

    CX_CHECK(_size > 0, "_size must be greater than zero!");
    CX_CHECK_NOT_NULL(_data);
    CX_CHECK_NOT_NULL(_comp);

    if (0 == _num) return;

    void* pivot = malloc(_size);
    void* temp = malloc(_size);

    _cx_sort(_data, _size, _num, _comp, _userData, pivot, temp);

    free(pivot);
    free(temp);
}

uint32_t cx_sort_find(void* _data, uint32_t _size, uint32_t _num, const void* _key, bool _firstMatch, cx_sort_comp_cb _comp, void* _userData)
{
    // searches for an element that is greater than or equal to the _key parameter using binary search
    // in the given _data array of _num elements of _size bytes each which is assumed to be sorted.
    // if _firstMatch is true, and _key is found, the returned value is guaranteed to be the first 
    // element in the array that matches the given _key.

    CX_CHECK(_size > 0, "_size must be greater than zero!");
    CX_CHECK_NOT_NULL(_data);
    CX_CHECK_NOT_NULL(_key);
    CX_CHECK_NOT_NULL(_comp);

    if (0 == _num) return -1; // element is not found, and it should be placed first in the list

    int32_t left = 0;
    int32_t right = _num;
    int32_t mid = 0;
    int32_t result = 0;
    bool found = false;
    char* data = (char*)_data;

    while (left <= right)
    {
        mid = (left + right) / 2;
        result = _comp(&data[mid * _size], _key, _userData);

        if (result < 0)
        {
            left = mid + 1;
        }
        else if (result > 0)
        {
            right = mid - 1;
        }
        else
        {
            if (!_firstMatch) return mid;
            found = true;
            right = mid; // keep searching for the first occurrence
        }
    }

    if (found)
    {
        return left;
    }
    else
    {
        // not found. return the negative position where it should be placed.
        // the position returned starts counting at index 1
        // so that we can represent index position 0 as (-1), 1 as (-2) and so on.
        return (-1) * left - 1;
    }
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static void _cx_swap(void* _a, void* _b, void* _temp, uint32_t _size)
{
    memcpy(_temp, _a, _size);
    memcpy(_a, _b, _size);
    memcpy(_b, _temp, _size);
}

static void _cx_sort(void* _data, uint32_t _size, uint32_t _num, cx_sort_comp_cb _comp, void* _userData, void* _pivot, void* _temp)
{
    int32_t i = -1;
    int32_t j = _num;
    int32_t last = _num - 1;
    char* data = (char*)_data;

    // take the pivot
    memcpy(_pivot, data, _size);

    while (i < j)
    {
        // find an i greater than pivot
        do
        {
            i++;
        } while (i < last && _comp(&data[i * _size], _pivot, _userData) < 0);

        // find a j lower than pivot
        do
        {
            j--;
        } while (j > 0 && _comp(&data[j * _size], _pivot, _userData) > 0);

        // if i and j didn't cross each other, swap them
        if (i < j)
        {
            _cx_swap(&data[i * _size], &data[j * _size], _temp, _size);
        }
    }

    if (j > 0)
    {
        // swap them
        _cx_swap(data, &data[j * _size], _temp, _size);

        // sort the left partition
        _cx_sort(data, _size, j + 1, _comp, _userData, _pivot, _temp);
    }

    // sort the right partition (it must have at least 2 elements)
    _num = last - j;
    if (_num > 1)
    {
        _cx_sort(&data[(j + 1) * _size], _size, _num, _comp, _userData, _pivot, _temp);
    }
}

uint32_t cx_sort_uniquify(void* _data, uint32_t _size, uint32_t _num, cx_sort_comp_cb _comp, void* _userData, cx_destroyer_cb _destroyer)
{
    // removes duplicates in-place from the sorted _data array of _num elements of 
    // _size bytes each. comparisons are made using _comp callback, and only 
    // the first unique occurrence of each element is preserved in the modified array.
    // you can use the optional parameter _destroyer if your elements contain references
    // and they must be freed before having their memory overwritten.
    // the returned value is the new number of elements in the modified _data array.

    CX_CHECK(_size > 0, "_size must be greater than zero!");
    CX_CHECK_NOT_NULL(_data);
    CX_CHECK_NOT_NULL(_comp);

    if (0 == _num) return 0;

    char* data = (char*)_data;
    uint32_t count = 0;

    for (uint32_t i = 1; i < _num; i++)
    {
        if (0 != _comp(&(data[i * _size]), &(data[count * _size]), _userData))
        {
            count++;
            memcpy(&(data[count * _size]), &(data[i * _size]), _size);
        }
        else if (NULL != _destroyer)
        {
            _destroyer(&(data[i * _size]));
        }
    }
    return ++count;
}