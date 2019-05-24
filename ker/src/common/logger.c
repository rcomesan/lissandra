#include <ker/logger.h>

bool logger_init(t_log** _log, cx_err_t* _err)
{
    cx_timestamp_t timestamp;
    cx_time_stamp(&timestamp);

    cx_path_t path;
    cx_file_path(&path, "logs");
    if (cx_file_mkdir(&path, _err))
    {
        cx_file_path(&path, "logs/%s.txt", timestamp);
        (*_log) = log_create(path, PROJECT_NAME, true, LOG_LEVEL_INFO);

        if (NULL != (*_log))
        {
            CX_INFO("log file: %s", path);
            return true;
        }

        CX_ERR_SET(_err, ERR_LOGGER_FAILED, "%s log initialization failed (%s).", PROJECT_NAME, path);
    }
    else
    {
        CX_ERR_SET(_err, ERR_LOGGER_FAILED, "%s logs folder creation failed (%s).", PROJECT_NAME, path);
    }
    return false;
}

void logger_destroy(t_log* _log)
{
    if (NULL != _log)
    {
        log_destroy(_log);
    }
}