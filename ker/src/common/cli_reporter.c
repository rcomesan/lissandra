#include <cx/cx.h>
#include <cx/timer.h>
#include <ker/cli_reporter.h>

#include <stdio.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

#define CLI_REPORT_BEGIN                                                    \
    float duration = cx_time_counter() - _task->startTime;

#define CLI_REPORT_END                                                      \
    printf("(%.3f sec)\n", duration);                                       \
    printf("\n");

void cli_report_error(cx_err_t* err)
{
    printf("%s\n\n", err->desc);
}


void cli_report_compact(const task_t* _task, double _blockedTime)
{
    data_compact_t* data = _task->data;
    if (ERR_NONE == _task->err.code)
    {
        printf("Table '%s' compacted successfully. (%.3f sec blocked)\n", data->tableName, _blockedTime);
    }
    else
    {
        printf("Table '%s' COMPACT failed. %s\n", data->tableName, _task->err.desc);
    }
}

void cli_report_dumped(const task_t* _task, const char* _tableName, const char* _fileName)
{
    if (ERR_NONE == _task->err.code)
    {
        printf("Table '%s' dumped successfully.\n", _tableName);
    }
    else
    {
        printf("Table '%s' DUMP failed. %s\n", _tableName, _task->err.desc);
    }
}

void cli_report_select(const task_t* _task)
{
    CLI_REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_select_t* data = _task->data;
        printf("%d: \"%s\".\n", data->record.key, data->record.value);
    }
    else
    {
        printf("SELECT failed. %s\n", _task->err.desc);
    }
    CLI_REPORT_END;
}

void cli_report_insert(const task_t* _task)
{
    CLI_REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_insert_t* data = _task->data;
        printf("%d: \"%s\".\n", data->record.key, data->record.value);
    }
    else
    {
        printf("INSERT failed. %s\n", _task->err.desc);
    }
    CLI_REPORT_END;
}

void cli_report_create(const task_t* _task)
{
    CLI_REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_create_t* data = _task->data;
        printf("Table '%s' created.\n", data->tableName);
    }
    else
    {
        printf("CREATE failed. %s\n", _task->err.desc);
    }
    CLI_REPORT_END;
}

void cli_report_describe(const task_t* _task)
{
    CLI_REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_describe_t* data = _task->data;

        printf("+--------------------------------+---------------+------------+---------------+\n");
        printf("| Name                           | Consistency   | Partitions | Compaction    |\n");
        printf("+--------------------------------+---------------+------------+---------------+\n");

        for (uint16_t i = 0; i < data->tablesCount; i++)
        {
            printf("| %-30s | %-13s | %-10d | %-13d |\n", 
                data->tables[i].name,
                CONSISTENCY_NAME[data->tables[i].consistency],
                data->tables[i].partitionsCount,
                data->tables[i].compactionInterval);
        }
        printf("+--------------------------------+---------------+------------+---------------+\n");
    }
    else
    {
        printf("DESCRIBE failed. %s\n", _task->err.desc);
    }
    CLI_REPORT_END;
}

void cli_report_drop(const task_t* _task)
{
    CLI_REPORT_BEGIN;
    if (ERR_NONE == _task->err.code)
    {
        data_drop_t* data = _task->data;
        printf("Table '%s' dropped.\n", data->tableName);
    }
    else
    {
        printf("DROP failed. %s\n", _task->err.desc);
    }
    CLI_REPORT_END;
}

void cli_report_run(const task_t* _task)
{
    printf("cli_report_run\n");
}

void cli_report_add_memory(const task_t* _task)
{
    printf("cli_report_add_memory\n");
}
