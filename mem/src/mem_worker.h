#ifndef MEM_WORKER_H_
#define MEM_WORKER_H_

#include <ker/taskman.h>

/****************************************************************************************
***  PUBLIC FUNCTIONS
***************************************************************************************/

void        worker_handle_create(task_t* _req);

void        worker_handle_drop(task_t* _req);

void        worker_handle_describe(task_t* _req);

void        worker_handle_select(task_t* _req);

void        worker_handle_insert(task_t* _req);

#endif // MEM_WORKER_H_
