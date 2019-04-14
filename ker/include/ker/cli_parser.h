#ifndef CLI_PARSER_H_
#define CLI_PARSER_H_

#include <cx/cli.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool cli_parse_select(const cx_cli_cmd_t* _cmd, char* _outError, char** _outTableName, uint16_t* _outKey);

bool cli_parse_insert(const cx_cli_cmd_t* _cmd, char* _outError, char** _outTableName, uint16_t* _outKey, char** _outValue, uint32_t* _outTimestamp);

bool cli_parse_create(const cx_cli_cmd_t* _cmd, char* _outError, char** _outTableName, uint8_t* _outConsistency, uint16_t* _outNumPartitions, uint32_t* _outCompactionTime);

bool cli_parse_describe(const cx_cli_cmd_t* _cmd, char* _outError, char** _outTableName);

bool cli_parse_drop(const cx_cli_cmd_t* _cmd, char* _outError, char** _outTableName);

bool cli_parse_run(const cx_cli_cmd_t* _cmd, char* _outError, char** _outLqlPath);

bool cli_parse_add_memory(const cx_cli_cmd_t* _cmd, char* _outError, uint16_t* _outMemNumber, uint8_t* _outConsistency);

#endif // CLI_PARSER_H_
