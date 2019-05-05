#ifndef LFS_WORKER_H
#define LFS_WORKER_H

#include "fs.h"

/****************************************************************************************
***  PUBLIC FUNCTIONS
***************************************************************************************/

void        worker_handle_create(task_t* _req);

void        worker_handle_drop(task_t* _req);

void        worker_handle_describe(task_t* _req);

void        worker_handle_select(task_t* _req);

void        worker_handle_insert(task_t* _req);

void        worker_handle_dump(task_t* _req);

void        worker_handle_compact(task_t* _req);

#endif // LFS_WORKER_H
