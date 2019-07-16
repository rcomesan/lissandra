#include <ker/common.h>
#include <ker/defines.h>

#include <cx/cx.h>
#include <cx/str.h>
#include <cx/math.h>

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
        data_create_t* data = (data_create_t*)_data;
        //noop
        break;
    }

    case TASK_WT_DROP:
    {
        data_drop_t* data = (data_drop_t*)_data;
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

    case TASK_WT_DUMP:
    {
        data_dump_t* data = (data_dump_t*)_data;
        //noop
        break;
    }

    case TASK_WT_COMPACT:
    {
        data_compact_t* data = (data_compact_t*)_data;
        //noop
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

    case TASK_MT_COMPACT:
    {
        //noop
        break;
    }

    case TASK_MT_DUMP:
    {
        //noop
        break;
    }

    case TASK_MT_JOURNAL:
    {
        //noop
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

    free(_data);
    return true;
}

bool cfg_get_uint8(t_config* _cfg, char* _key, uint8_t* _out)
{
    if (config_has_property(_cfg, _key))
    {
        *_out = (uint8_t)config_get_int_value(_cfg, _key);
        return true;
    }
    return false;
}

bool cfg_get_uint16(t_config* _cfg, char* _key, uint16_t* _out)
{
    if (config_has_property(_cfg, _key))
    {
        *_out = (uint16_t)config_get_int_value(_cfg, _key);
        return true;
    }
    return false;
}

bool cfg_get_uint32(t_config* _cfg, char* _key, uint32_t* _out)
{
    if (config_has_property(_cfg, _key))
    {
        *_out = (uint32_t)config_get_int_value(_cfg, _key);
        return true;
    }
    return false;
}

bool cfg_get_password(t_config* _cfg, char* _key, password_t* _out)
{
    if (config_has_property(_cfg, _key))
    {
        char* temp = config_get_string_value(_cfg, _key);

        uint32_t len = strlen(temp);
        CX_WARN(len >= MIN_PASSWD_LEN, "'%s' must have a minimum length of %d characters!", _key, MIN_PASSWD_LEN);
        CX_WARN(len <= MAX_PASSWD_LEN, "'%s' must have a maximum length of %s characters!", _key, MAX_PASSWD_LEN)
        
        if (cx_math_in_range(len, MIN_PASSWD_LEN, MAX_PASSWD_LEN))
        {
            cx_str_copy(*_out, sizeof(*_out), temp);
            return true;
        }
    }
    return false;
}

bool cfg_get_string(t_config* _cfg, char* _key, char* _out, uint32_t _size)
{
    if (config_has_property(_cfg, _key))
    {
        char* temp = config_get_string_value(_cfg, _key);
        cx_str_copy(_out, _size, temp);

        return true;
    }
    return false;
}