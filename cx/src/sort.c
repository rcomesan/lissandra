#include "sort.h"

#include <string.h>

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static void _cx_sort(void* _data, uint32_t _size, uint32_t _num, cx_sort_comp_cb _comp, void* _pivot, void* _temp);

static void _cx_swap(void* _a, void* _b, void* _temp, uint32_t _size);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void cx_sort_quick(void* _data, uint32_t _size, uint32_t _num, cx_sort_comp_cb _comp)
{
    // sorts the _data array of _num elements of _size bytes each using quicksort algorithm.
    // comparisons are made using _comp callback which should return:
    // a value lesss than zero      if _a should be placed to the left of _b.
    // a value greater than zero    if _a should be placed to the right of _b.
    // a value equal to zero        if _a and b are equal.

    void* pivot = malloc(_size);
    void* temp = malloc(_size);

    _cx_sort(_data, _size, _num, _comp, pivot, temp);

    free(pivot);
    free(temp);
}

uint32_t cx_sort_find(void* _data, uint32_t _size, uint32_t _num, const void* _key, cx_sort_comp_cb _comp)
{
    // searches for the first element that is greater than or equal to the _key parameter using binary search
    // in the given _data array of _num elements of _size bytes each which is assumed to be sorted.

    int32_t left = 0;
    int32_t right = _num;
    int32_t mid = 0;
    int32_t result = 0;
    char* data = (char*)_data;

    while (left < right)
    {
        mid = (left + right) / 2;
        result = _comp(&data[mid * _size], _key);

        if (result >= 0)
        {
            right = mid;
        }
        else
        {
            left = mid + 1;
        }
    }

    result = _comp(&data[left * _size], _key);
    if (0 == result)
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

static void _cx_sort(void* _data, uint32_t _size, uint32_t _num, cx_sort_comp_cb _comp, void* _pivot, void* _temp)
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
        } while (i < last && _comp(&data[i * _size], _pivot) < 0);

        // find a j lower than pivot
        do
        {
            j--;
        } while (j > 0 && _comp(&data[j * _size], _pivot) > 0);

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
        _cx_sort(data, _size, j + 1, _comp, _pivot, _temp);
    }

    // sort the right partition (it must have at least 2 elements)
    _num = last - j;
    if (_num > 1)
    {
        _cx_sort(&data[(j + 1) * _size], _size, _num, _comp, _pivot, _temp);
    }
}
