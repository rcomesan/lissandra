#ifndef CLI_REPORTER_H_
#define CLI_REPORTER_H_

#include "defines.h"

#include <cx/cx.h>
#include <ker/taskman.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void cli_report_error(cx_err_t* err);

void cli_report_info(const char* _info);

void cli_report_select(const task_t* _task);

void cli_report_insert(const task_t* _task);

void cli_report_create(const task_t* _task);

void cli_report_describe(const task_t* _task);

void cli_report_drop(const task_t* _task);

void cli_report_run(const task_t* _task);

void cli_report_add_memory(const task_t* _task);

#endif // CLI_REPORTER_H_
