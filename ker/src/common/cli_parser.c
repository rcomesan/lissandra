#include <ker/cli_parser.h>
#include <ker/defines.h>

#include <cx/cx.h>
#include <cx/str.h>
#include <cx/math.h>

#include <ctype.h>
#include <string.h>
#include <stdint.h>

/****************************************************************************************
 ***  PRIVATE DECLARATIONS
 ***************************************************************************************/

static bool     valid_key(const char* _str);

static bool     valid_table(const char* _str);

static bool     valid_timestamp(const char* _str);

static bool     valid_consistency(const char* _str);

static bool     valid_partitions_number(const char* _str);

static bool     valid_compaction_interval(const char* _str);

static bool     valid_memory_number(const char* _str);

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool cli_parse_select(const cx_cli_cmd_t* _cmd, char* _outError, char** _outTableName, uint16_t* _outKey)
{
    CX_CHECK(0 == strcmp("SELECT", _cmd->header), "invalid command!");

    if (true
        && _cmd->argsCount >= 2
        && valid_table(_cmd->args[0])
        && valid_key(_cmd->args[1]))
    {
        (*_outTableName) = _cmd->args[0];
        cx_str_parse_uint16(_cmd->args[1], _outKey);
        return true;
    }

    cx_str_format(_outError, CLI_PARSER_ERROR_LEN,
        "Invalid Syntax. Usage: SELECT [TABLE_NAME] [KEY]");
    return false;
}

bool cli_parse_insert(const cx_cli_cmd_t* _cmd, char* _outError, char** _outTableName, uint16_t* _outKey, char** _outValue, uint32_t* _outTimestamp)
{
    CX_CHECK(0 == strcmp("INSERT", _cmd->header), "invalid command!");

    if (true
        && _cmd->argsCount >= 3
        && valid_table(_cmd->args[0])
        && valid_key(_cmd->args[1])
        && _cmd->argsCount >= 4 ? valid_timestamp(_cmd->args[3]) : true)
    {
        (*_outTableName) = _cmd->args[0];
        cx_str_parse_uint16(_cmd->args[1], _outKey);
        (*_outValue) = _cmd->args[2];
        (*_outTimestamp) = cx_time_stamp();

        if (_cmd->argsCount >= 4)
        {
            cx_str_parse_uint32(_cmd->args[3], _outTimestamp);
        }
        
        return true;
    }

    cx_str_format(_outError, CLI_PARSER_ERROR_LEN,
        "Invalid Syntax. Usage: INSERT [TABLE_NAME] [KEY] \"[VALUE]\" (TIMESTAMP)");
    return false;
}

bool cli_parse_create(const cx_cli_cmd_t* _cmd, char* _outError, char** _outTableName, uint8_t* _outConsistency, uint16_t* _outNumPartitions, uint32_t* _outCompactionInterval)
{
    CX_CHECK(0 == strcmp("CREATE", _cmd->header), "invalid command!");

    if (true
        && _cmd->argsCount >= 4
        && valid_table(_cmd->args[0])
        && valid_consistency(_cmd->args[1])
        && valid_partitions_number(_cmd->args[2])
        && valid_compaction_interval(_cmd->args[3]))
    {
        (*_outTableName) = _cmd->args[0];
        cx_str_parse_uint8(_cmd->args[1], _outConsistency);
        cx_str_parse_uint16(_cmd->args[2], _outNumPartitions);
        cx_str_parse_uint32(_cmd->args[3], _outCompactionInterval);

        return true;
    }

    cx_str_format(_outError, CLI_PARSER_ERROR_LEN,
        "Invalid Syntax. Usage: CREATE [TABLE_NAME] [CONSISTENCY] [NUM_PARTITIONS] [COMPACTION_INTERVAL]");
    return false;
}

bool cli_parse_describe(const cx_cli_cmd_t* _cmd, char* _outError, char** _outTableName)
{
    CX_CHECK(0 == strcmp("DESCRIBE", _cmd->header), "invalid command!");

    if (_cmd->argsCount >= 1)
    {
        // specific table requested, validate it
        if (valid_table(_cmd->args[0]))
        {
            (*_outTableName) = _cmd->args[0];
            return true;
        }
    }
    else
    {
        // all tables requested
        (*_outTableName) = NULL;
        return true;
    }

    cx_str_format(_outError, CLI_PARSER_ERROR_LEN,
        "Invalid Syntax. Usage: DESCRIBE (TABLE_NAME)");
    return false;
}

bool cli_parse_drop(const cx_cli_cmd_t* _cmd, char* _outError, char** _outTableName)
{
    CX_CHECK(0 == strcmp("DROP", _cmd->header), "invalid command!");

    if (true
        && _cmd->argsCount >= 1
        && valid_table(_cmd->args[0]))
    {
        (*_outTableName) = _cmd->args[0];
        return true;
    }

    cx_str_format(_outError, CLI_PARSER_ERROR_LEN,
        "Invalid Syntax. Usage: DROP [TABLE_NAME]");
    return false;
}

bool cli_parse_run(const cx_cli_cmd_t* _cmd, char* _outError, char** _outLqlPath)
{
    CX_CHECK(0 == strcmp("RUN", _cmd->header), "invalid command!");

    if (true
        && _cmd->argsCount >= 1)
    {
        (*_outLqlPath) = _cmd->args[0];
        return true;
    }

    cx_str_format(_outError, CLI_PARSER_ERROR_LEN,
        "Invalid Syntax. Usage: RUN [LQL_FILE_PATH]");
    return false;
}

bool cli_parse_add_memory(const cx_cli_cmd_t* _cmd, char* _outError, uint16_t* _outMemNumber, uint8_t* _outConsistency)
{
    CX_CHECK(0 == strcmp("ADD", _cmd->header), "invalid command!");

    if (true
        && _cmd->argsCount >= 4
        && (0 == strcasecmp("MEMORY", _cmd->args[0]))
        && valid_memory_number(_cmd->args[1])
        && (0 == strcasecmp("TO", _cmd->args[2]))
        && valid_consistency(_cmd->args[3]))
    {
        cx_str_parse_uint16(_cmd->args[1], _outMemNumber);
        cx_str_parse_uint8(_cmd->args[3], _outConsistency);
    }

    cx_str_format(_outError, CLI_PARSER_ERROR_LEN,
        "Invalid Syntax. Usage: ADD MEMORY [MEM_NUMBER] TO [CONSISTENCY]");
    return false;
}

/****************************************************************************************
 ***  PRIVATE FUNCTIONS
 ***************************************************************************************/

static bool valid_table(const char* _str)
{
    return cx_math_in_range(strlen(_str), TABLE_NAME_LEN_MIN, TABLE_NAME_LEN_MAX);
}

static bool valid_key(const char* _str)
{
    uint16_t ui16 = 0;
    return cx_str_parse_uint16(_str, &ui16);
}

static bool valid_timestamp(const char* _str)
{
    uint32_t ui32 = 0;
    return cx_str_parse_uint32(_str, &ui32);
}

static bool valid_consistency(const char* _str)
{
    uint8_t ui8 = 0;
    return true
        && cx_str_parse_uint8(_str, &ui8)
        && cx_math_in_range(ui8, 1, 3); //TODO CHECKME. consistency enum
}

static bool valid_partitions_number(const char* _str)
{
    uint16_t ui16 = 0;
    return cx_str_parse_uint16(_str, &ui16);
}

static bool valid_compaction_interval(const char* _str)
{
    uint32_t ui32 = 0;
    return cx_str_parse_uint32(_str, &ui32);
}

static bool valid_memory_number(const char* _str)
{
    uint16_t ui16 = 0;
    return cx_str_parse_uint16(_str, &ui16);
}