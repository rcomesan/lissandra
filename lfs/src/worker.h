#ifndef LFS_WORKER_H
#define LFS_WORKER_H

#include "lfs.h"

/****************************************************************************************
***  PUBLIC FUNCTIONS
***************************************************************************************/

void        worker_handle_create(request_t* _req);

void        worker_handle_drop(request_t* _req);

void        worker_handle_describe(request_t* _req);

void        worker_handle_select(request_t* _req);

void        worker_handle_insert(request_t* _req);

#endif // LFS_WORKER_H
