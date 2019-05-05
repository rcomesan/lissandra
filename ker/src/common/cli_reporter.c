#include <cx/cx.h>
#include <cx/timer.h>
#include <ker/cli_reporter.h>

#include <stdio.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

#define CLI_REPORT_BEGIN                                                    \
    float duration = cx_time_counter() - _result->c.startTime;

#define CLI_REPORT_END                                                      \
    printf("(%.3f sec)\n", duration);                                       \
    printf("\n");

void cli_report_error(cx_err_t* err)
{
    printf("%s\n\n", err->desc);
}

void cli_report_select(const data_select_t* _result)
{
    CLI_REPORT_BEGIN;
    if (ERR_NONE == _result->c.err.code)
    {
        printf("%d: \"%s\".\n", _result->record.key, _result->record.value);
    }
    else
    {
        printf("SELECT failed. %s\n", _result->c.err.desc);
    }
    CLI_REPORT_END;
}

void cli_report_insert(const data_insert_t* _result)
{
    CLI_REPORT_BEGIN;
    if (ERR_NONE == _result->c.err.code)
    {
        printf("%d: \"%s\".\n", _result->record.key, _result->record.value);
    }
    else
    {
        printf("INSERT failed. %s\n", _result->c.err.desc);
    }
    CLI_REPORT_END;
}

void cli_report_create(const data_create_t* _result)
{
    CLI_REPORT_BEGIN;
    if (ERR_NONE == _result->c.err.code)
    {
        printf("Table '%s' created.\n", _result->tableName);
    }
    else
    {
        printf("CREATE failed. %s\n", _result->c.err.desc);
    }
    CLI_REPORT_END;
}

void cli_report_describe(const data_describe_t* _result)
{
    CLI_REPORT_BEGIN;
    if (ERR_NONE == _result->c.err.code)
    {
        printf("+--------------------------------+---------------+------------+---------------+\n");
        printf("| Name                           | Consistency   | Partitions | Compaction    |\n");
        printf("+--------------------------------+---------------+------------+---------------+\n");

        for (uint16_t i = 0; i < _result->tablesCount; i++)
        {
            printf("| %-30s | %-13s | %-10d | %-13d |\n", 
                _result->tables[i].name,
                CONSISTENCY_NAME[_result->tables[i].consistency],
                _result->tables[i].partitionsCount,
                _result->tables[i].compactionInterval);
        }
        printf("+--------------------------------+---------------+------------+---------------+\n");
    }
    else
    {
        printf("DESCRIBE failed. %s\n", _result->c.err.desc);
    }
    CLI_REPORT_END;
}

void cli_report_drop(const data_drop_t* _result)
{
    CLI_REPORT_BEGIN;
    if (ERR_NONE == _result->c.err.code)
    {
        printf("Table '%s' dropped.\n", _result->tableName);
    }
    else
    {
        printf("DROP failed. %s\n", _result->c.err.desc);
    }
    CLI_REPORT_END;
}

void cli_report_run(const data_run_t* _result)
{
    printf("cli_report_run\n");
}

void cli_report_add_memory(const data_add_memory_t* _result)
{
    printf("cli_report_add_memory\n");
}
