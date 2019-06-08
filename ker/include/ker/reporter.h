#ifndef CLI_REPORTER_H_
#define CLI_REPORTER_H_

#include "defines.h"

#include <cx/cx.h>
#include <ker/taskman.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void report_error(cx_err_t* err, FILE* _stream);

void report_info(const char* _info, FILE* _stream);

void report_select(const task_t* _task, FILE* _stream);

void report_insert(const task_t* _task, FILE* _stream);

void report_create(const task_t* _task, FILE* _stream);

void report_describe(const task_t* _task, FILE* _stream);

void report_drop(const task_t* _task, FILE* _stream);

void report_addmem(const task_t* _task, FILE* _stream);

void report_run(const task_t* _task, FILE* _stream);

void report_add_memory(const task_t* _task, FILE* _stream);

#endif // CLI_REPORTER_H_
