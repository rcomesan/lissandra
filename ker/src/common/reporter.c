#include <cx/cx.h>
#include <cx/timer.h>
#include <ker/reporter.h>

#include <stdio.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

#define REPORT_BEGIN                                                                    \
    float duration = cx_time_counter() - _task->startTime;

#define REPORT_END                                                                      \
    fprintf(_stream, "(%.3f sec)\n", duration);                                         \
    fprintf(_stream, "\n");

void report_error(cx_err_t* err, FILE* _stream)
{
    fprintf(_stream, "%s\n\n", err->desc);
}

void report_info(const char* _info, FILE* _stream)
{
    fprintf(_stream, "%s\n\n", _info);
}

void report_select(const task_t* _task, FILE* _stream)
{
    REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_select_t* data = _task->data;
        fprintf(_stream, "%d: \"%s\".\n", data->record.key, data->record.value);
    }
    else
    {
        fprintf(_stream, "SELECT failed. %s\n", _task->err.desc);
    }
    REPORT_END;
}

void report_insert(const task_t* _task, FILE* _stream)
{
    REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_insert_t* data = _task->data;
        fprintf(_stream, "%d: \"%s\".\n", data->record.key, data->record.value);
    }
    else
    {
        fprintf(_stream, "INSERT failed. %s\n", _task->err.desc);
    }
    REPORT_END;
}

void report_create(const task_t* _task, FILE* _stream)
{
    REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_create_t* data = _task->data;
        fprintf(_stream, "Table '%s' created.\n", data->tableName);
    }
    else
    {
        fprintf(_stream, "CREATE failed. %s\n", _task->err.desc);
    }
    REPORT_END;
}

void report_describe(const task_t* _task, FILE* _stream)
{
    REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_describe_t* data = _task->data;

        fprintf(_stream, "+--------------------------------+---------------+------------+---------------+\n");
        fprintf(_stream, "| Name                           | Consistency   | Partitions | Compaction    |\n");
        fprintf(_stream, "+--------------------------------+---------------+------------+---------------+\n");

        for (uint16_t i = 0; i < data->tablesCount; i++)
        {
            fprintf(_stream, "| %-30s | %-13s | %-10d | %-13d |\n",
                data->tables[i].name,
                CONSISTENCY_NAME[data->tables[i].consistency],
                data->tables[i].partitionsCount,
                data->tables[i].compactionInterval);
        }
        fprintf(_stream, "+--------------------------------+---------------+------------+---------------+\n");
    }
    else
    {
        fprintf(_stream, "DESCRIBE failed. %s\n", _task->err.desc);
        
    }
    REPORT_END;
}

void report_drop(const task_t* _task, FILE* _stream)
{
    REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_drop_t* data = _task->data;
        fprintf(_stream, "Table '%s' dropped.\n", data->tableName);
    }
    else
    {
        fprintf(_stream, "DROP failed. %s\n", _task->err.desc);
    }
    REPORT_END;
}

void report_addmem(const task_t* _task, FILE* _stream)
{
    REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_addmem_t* data = _task->data;
        fprintf(_stream, "MEM node #%d successfully assigned to %s consistency.\n",
            data->memNumber, CONSISTENCY_NAME[data->consistency]);
    }
    else
    {
        fprintf(_stream, "ADD MEMORY failed. %s\n", _task->err.desc);
    }
    REPORT_END;
}

void report_run(const cx_path_t* _lqlFilePath, const cx_path_t* _logPath, FILE* _stream)
{
    cx_path_t scriptName;
    cx_file_get_name(_lqlFilePath, false, &scriptName);
   
    fprintf(_stream, "Script '%s' scheduled. Output will be written to '%s'.\n", scriptName, *_logPath);
}

void report_add_memory(const task_t* _task, FILE* _stream)
{
    fprintf(_stream, "report_add_memory\n");
}
