#include <ker/common.h>
#include <cx/cx.h>
#include <ker/defines.h>

#include <string.h>

QUERY_TYPE common_parse_query(const char* _queryHead)
{
    if (strcasecmp("EXIT", _queryHead) == 0)
    {
        return QUERY_EXIT;
    }
    else if (strcasecmp("LOGFILE", _queryHead) == 0)
    {
        return QUERY_LOGFILE;
    }
    else if (strcasecmp("CREATE", _queryHead) == 0)
    {
        return QUERY_CREATE;
    }
    else if (strcasecmp("DROP", _queryHead) == 0)
    {
        return QUERY_DROP;
    }
    else if (strcasecmp("DESCRIBE", _queryHead) == 0)
    {
        return QUERY_DESCRIBE;
    }
    else if (strcasecmp("SELECT", _queryHead) == 0)
    {
        return QUERY_SELECT;
    }
    else if (strcasecmp("INSERT", _queryHead) == 0)
    {
        return QUERY_INSERT;
    }
    else if (strcasecmp("JOURNAL", _queryHead) == 0)
    {
        return QUERY_JOURNAL;
    }
    else if (strcasecmp("RUN", _queryHead) == 0)
    {
        return QUERY_RUN;
    }
    else if (strcasecmp("ADD", _queryHead) == 0)
    {
        return QUERY_ADDMEMORY;
    }
    else
    {
        return QUERY_NONE;
    }
}

bool common_task_data_free(TASK_TYPE _type, void* _data)
{
    switch (_type)
    {
    case TASK_WT_CREATE:
    {
        //noop
        break;
    }

    case TASK_WT_DROP:
    {
        //noop
        break;
    }

    case TASK_WT_DESCRIBE:
    {
        data_describe_t* data = (data_describe_t*)_data;
        free(data->tables);
        data->tables = NULL;
        data->tablesCount = 0;
        break;
    }

    case TASK_WT_SELECT:
    {
        data_select_t* data = (data_select_t*)_data;
        free(data->record.value);
        data->record.value = NULL;
        break;
    }

    case TASK_WT_INSERT:
    {
        data_insert_t* data = (data_insert_t*)_data;
        free(data->record.value);
        data->record.value = NULL;
        break;
    }

    case TASK_WT_JOURNAL:
    {
        //noop
        break;
    }

    case TASK_WT_ADDMEM:
    {
        //noop
        break;
    }

    case TASK_WT_RUN:
    {
        data_run_t* data = (data_run_t*)_data;

        if (NULL != data->script)
        {
            cx_linesf_close(data->script);
            data->script = NULL;
        }

        if (NULL != data->output)
        {
            fclose(data->output);
            data->output = NULL;
        }

        break;
    }

    case TASK_MT_FREE:
    {
        //noop
        break;
    }

    default:
        CX_WARN(CX_ALW, "undefined <free> behaviour for request type #%d.", _type);
        break;
    }

    return true;
}