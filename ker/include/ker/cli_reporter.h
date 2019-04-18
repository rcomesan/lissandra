#ifndef CLI_REPORTER_H_
#define CLI_REPORTER_H_

#include "defines.h"
#include <cx/cx.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void cli_report_error(cx_error_t* err);

void cli_report_select(const data_select_t* _result);

void cli_report_insert(const data_insert_t* _result);

void cli_report_create(const data_create_t* _result);

void cli_report_describe(const data_describe_t* _result);

void cli_report_drop(const data_drop_t* _result);

void cli_report_run(const data_run_t* _result);

void cli_report_add_memory(const data_add_memory_t* _result);

#endif // CLI_REPORTER_H_
