#ifndef REPORTER_H_
#define REPORTER_H_

#include "defines.h"

#include <cx/cx.h>
#include <ker/taskman.h>

#include "../src/mempool.h"

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

void report_error(cx_err_t* err, FILE* _stream);

void report_info(const char* _info, FILE* _stream);

void report_query(const char* _query, FILE* _stream);

void report_select(const task_t* _task, FILE* _stream);

void report_insert(const task_t* _task, FILE* _stream);

void report_create(const task_t* _task, FILE* _stream);

void report_describe(const task_t* _task, FILE* _stream);

void report_drop(const task_t* _task, FILE* _stream);

void report_journal(const task_t* _task, FILE* _stream);

void report_addmem(const task_t* _task, FILE* _stream);

void report_run(const cx_path_t* _lqlFilePath, const cx_path_t* _logPath, FILE* _stream);

void report_add_memory(const task_t* _task, FILE* _stream);

void report_metrics(const mempool_metrics_t* _mtr, FILE* _stream);

void report_end(float _duration, FILE* _stream);

#endif // REPORTER_H_
