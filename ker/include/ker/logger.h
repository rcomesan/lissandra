#ifndef LOGGER_H_
#define LOGGER_H_

#include "defines.h"

#include <cx/cx.h>
#include <cx/timer.h>
#include <cx/file.h>

#include <stdbool.h>
#include <stdint.h>

#include <commons/log.h>

/****************************************************************************************
 ***  PUBLIC FUNCTIONS
 ***************************************************************************************/

bool        logger_init(t_log** _log, cx_err_t* _err);

void        logger_destroy(t_log* _log);

#endif // LOGGER_H_
