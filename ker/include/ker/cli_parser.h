#ifndef CLI_PARSER_H_
#define CLI_PARSER_H_

#include <cx/cx.h>
#include <cx/cli.h>
#include <cx/file.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool cli_parse_select(const cx_cli_cmd_t* _cmd, cx_err_t* _err, char** _outTableName, uint16_t* _outKey);

bool cli_parse_insert(const cx_cli_cmd_t* _cmd, cx_err_t* _err, char** _outTableName, uint16_t* _outKey, char** _outValue, uint32_t* _outTimestamp);

bool cli_parse_create(const cx_cli_cmd_t* _cmd, cx_err_t* _err, char** _outTableName, uint8_t* _outConsistency, uint16_t* _outNumPartitions, uint32_t* _outCompactionInterval);

bool cli_parse_describe(const cx_cli_cmd_t* _cmd, cx_err_t* _err, char** _outTableName);

bool cli_parse_drop(const cx_cli_cmd_t* _cmd, cx_err_t* _err, char** _outTableName);

bool cli_parse_run(const cx_cli_cmd_t* _cmd, cx_err_t* _err, cx_path_t* _outLqlPath);

bool cli_parse_add_memory(const cx_cli_cmd_t* _cmd, cx_err_t* _err, uint16_t* _outMemNumber, uint8_t* _outConsistency);

#endif // CLI_PARSER_H_
