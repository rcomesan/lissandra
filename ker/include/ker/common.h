#ifndef COMMON_H_
#define COMMON_H_

#include <ker/defines.h>
#include <ker/taskman.h>

QUERY_TYPE  common_parse_query(const char* _queryHead);

bool        common_task_data_free(TASK_TYPE _type, void* _data);

#endif // COMMON_H_