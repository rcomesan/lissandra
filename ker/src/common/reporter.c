#include <cx/cx.h>
#include <cx/timer.h>
#include <ker/reporter.h>

#include <stdio.h>
#include <inttypes.h>

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

void report_query(const char* _query, FILE* _stream)
{
    fprintf(_stream, ">>> %s\n", _query);
}

void report_select(const task_t* _task, FILE* _stream)
{
    REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_select_t* data = _task->data;
        fprintf(_stream, "%" PRIu16 ": \"%s\".\n", data->record.key, data->record.value);
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
        fprintf(_stream, "%" PRIu16 ": \"%s\".\n", data->record.key, data->record.value);
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
            fprintf(_stream, "| %-30s | %-13s | %-10" PRIu16 " | %-13" PRIu32 " |\n",
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

void report_journal(const task_t* _task, FILE* _stream)
{
    REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        fprintf(_stream, "Memory journal completed successfully.\n");
    }
    else
    {
        fprintf(_stream, "JOURNAL failed. %s\n", _task->err.desc);
    }
    REPORT_END;
}

void report_addmem(const task_t* _task, FILE* _stream)
{
    REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_addmem_t* data = _task->data;
        fprintf(_stream, "MEM node #%" PRIu16 " successfully assigned to %s consistency.\n",
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

void report_metrics(const mempool_metrics_t* _mtr, FILE* _stream)
{
    fprintf(_stream, "+---------------+--------+--------+-----------+-----------+\n");
    fprintf(_stream, "| Consistency   | Reads  | Writes | R/Latency | W/Latency |\n");
    fprintf(_stream, "+---------------+--------+--------+-----------+-----------+\n");
    for (uint32_t i = 0; i < CONSISTENCY_COUNT; i++)
    {
        printf("| %-13s | %-6" PRIu32 " | %-6" PRIu32 " | %-9.3f | %-9.3f |\n",
            CONSISTENCY_NAME[i],
            _mtr->reads[i],
            _mtr->writes[i],
            _mtr->readLatency[i],
            _mtr->writeLatency[i]);
    }
    fprintf(_stream, "+---------------+--------+--------+-----------+-----------+\n");

    fprintf(_stream, "\n");

    fprintf(_stream, "+-----+-------------+\n");
    fprintf(_stream, "| #   | Memory Load |\n");
    fprintf(_stream, "+-----+-------------+\n");
    for (uint32_t i = 1; i < MAX_MEM_NODES; i++)
    {
        if (-1 != _mtr->memLoad[i])
            printf("| %-3" PRIu32 " | %-11.2f |\n", i, _mtr->memLoad[i] * 100.0f);
    }
    fprintf(_stream, "+-----+-------------+\n");

    report_end(0, _stream);
}

void report_end(float _duration, FILE* _stream)
{
    float duration = _duration;
    REPORT_END;
}