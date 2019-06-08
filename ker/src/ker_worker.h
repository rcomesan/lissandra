#ifndef KER_WORKER_H_
#define KER_WORKER_H_

#include <ker/taskman.h>

/****************************************************************************************
***  PUBLIC FUNCTIONS
***************************************************************************************/

void        worker_handle_create(task_t* _req, bool _scripted);

void        worker_handle_drop(task_t* _req, bool _scripted);

void        worker_handle_describe(task_t* _req, bool _scripted);

void        worker_handle_select(task_t* _req, bool _scripted);

void        worker_handle_insert(task_t* _req, bool _scripted);

void        worker_handle_journal(task_t* _req, bool _scripted);

void        worker_handle_addmem(task_t* _req, bool _scripted);

void        worker_handle_run(task_t* _req);

#endif // KER_WORKER_H_
