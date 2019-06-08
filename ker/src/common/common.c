#include <ker/common.h>
#include <cx/cx.h>
#include <ker/defines.h>

bool task_data_free(TASK_TYPE _type, void* _data)
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