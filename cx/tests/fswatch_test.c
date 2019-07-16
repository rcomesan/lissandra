#include "test.h"
#include "fswatch.h"
#include "file.h"

#define     FSWATCH_FOLDER_PATH                "/tmp/cx-tests-fswatch"
#define     FSWATCH_FILE_PATH_A                 FSWATCH_FOLDER_PATH "/test-a.txt"
#define     FSWATCH_FILE_PATH_B                 FSWATCH_FOLDER_PATH "/test-b.txt"

int32_t     lastFswatchEventId = -1;
int32_t     lastFswatchEventMask = -1;
uint16_t    fswatchFileHandle = INVALID_HANDLE;
uint16_t    fswatchFolderHandle = INVALID_HANDLE;
uint32_t    fswatchTotalEvents = 0;

static void _t_fswatch_handler(const char* _path, uint32_t _mask, void* _userData)
{
    fswatchTotalEvents++;
    lastFswatchEventId = (int32_t)_userData;

    if (1 == lastFswatchEventId)        // t_fswatch_should_detect_changes_in_file
    {
        CU_ASSERT(_mask & IN_MODIFY);
        CU_ASSERT_STRING_EQUAL(_path, FSWATCH_FILE_PATH_A);
    }
    else if (2 == lastFswatchEventId)   // t_fswatch_should_detect_changes_in_folder
    {
        CU_ASSERT(_mask & IN_CREATE);
        CU_ASSERT_STRING_EQUAL(_path, FSWATCH_FILE_PATH_B);
    }
    else
    {
        CU_FAIL("unknown userData/eventId!");
    }
}

int t_fswatch_init()
{
    bool success = true
        && cx_fswatch_init(10, (cx_fswatch_handler_cb)_t_fswatch_handler, NULL)
        && cx_file_remove((cx_path_t*)FSWATCH_FOLDER_PATH, NULL)
        && cx_file_touch((cx_path_t*)FSWATCH_FILE_PATH_A, NULL);

    return CUNIT_RESULT(success);
}

int t_fswatch_cleanup()
{
    bool success = true;
    cx_file_remove((cx_path_t*)FSWATCH_FOLDER_PATH, NULL);
    cx_fswatch_destroy();
    return CUNIT_RESULT(success);
}

void t_fswatch_should_detect_changes_in_file()
{
    fswatchFileHandle = cx_fswatch_add(FSWATCH_FILE_PATH_A, IN_MODIFY, (void*)0x01);

    if (cx_file_write((cx_path_t*)FSWATCH_FILE_PATH_A, "hello", 6, NULL))
    {
        cx_fswatch_poll_events();
        CU_ASSERT(1 == lastFswatchEventId);

        lastFswatchEventId = -1;
        cx_fswatch_poll_events();
        CU_ASSERT(-1 == lastFswatchEventId);
    }
    else
    {
        CU_FAIL("cx_file_write failed!");
    }
}

void t_fswatch_should_detect_changes_in_folder()
{
    fswatchFolderHandle = cx_fswatch_add(FSWATCH_FOLDER_PATH, IN_CREATE, (void*)0x02);

    if (cx_file_touch((cx_path_t*)FSWATCH_FILE_PATH_B, NULL))
    {
        cx_fswatch_poll_events();
        CU_ASSERT(2 == lastFswatchEventId);

        lastFswatchEventId = -1;
        cx_fswatch_poll_events();
        CU_ASSERT(-1 == lastFswatchEventId);
    }
    else
    {
        CU_FAIL("cx_file_touch failed!");
    }
}

void t_fswatch_should_remove_entries()
{
    cx_fswatch_remove(fswatchFileHandle);
    cx_fswatch_remove(fswatchFolderHandle);
    //CU_ASSERT(0 == cx_fswatch_count());
}
