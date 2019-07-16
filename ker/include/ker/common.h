#ifndef COMMON_H_
#define COMMON_H_

#include <ker/defines.h>
#include <ker/taskman.h>

#include <commons/config.h>

QUERY_TYPE  common_parse_query(const char* _queryHead);

bool        common_task_data_free(TASK_TYPE _type, void* _data);

bool        cfg_get_uint8(t_config* _cfg, char* _key, uint8_t* _out);

bool        cfg_get_uint16(t_config* _cfg, char* _key, uint16_t* _out);

bool        cfg_get_uint32(t_config* _cfg, char* _key, uint32_t* _out);

bool        cfg_get_password(t_config* _cfg, char* _key, password_t* _out);

bool        cfg_get_string(t_config* _cfg, char* _key, char* _out, uint32_t _size);

#endif // COMMON_H_