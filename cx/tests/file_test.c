#include "test.h"
#include "file.h"
#include "mem.h"
#include "str.h"

#include <unistd.h>
#include <sys/stat.h>

#define     TEST_FOLDER_PATH                "/tmp/cx_tests-folder"
#define     TEST_FILE_PATH                  "/tmp/cx-tests-file.txt"
#define     TEST_FILE_CONTENT               "hello"
#define     TEST_FILE_CONTENT_LEN           5
FILE*       testFile = NULL;

#define     FILE_NAME                       "hello.txt"
#define     FILE_NAME_NO_EXTENSION          "hello"

cx_path_t   tmpPath;
cx_path_t   cwdPath;

int t_file_init()
{
    bool success = false;

    remove(TEST_FOLDER_PATH);
    remove(TEST_FILE_PATH);

    if (cwdPath == getcwd(cwdPath, sizeof(cwdPath))
        && 0 == mkdir(TEST_FOLDER_PATH, 0700))
    {
        testFile = fopen(TEST_FILE_PATH, "w");
        if (NULL != testFile)
        {
            success = 1 == fputs(TEST_FILE_CONTENT, testFile);
            fflush(testFile);
        }
    }

    return CUNIT_RESULT(success);
}

int t_file_cleanup()
{
    bool success = true;

    if (NULL != testFile)
    {
        fclose(testFile);
    }

    remove(TEST_FOLDER_PATH);
    remove(TEST_FILE_PATH);

    return CUNIT_RESULT(success);
}

void t_file_should_absolutize_paths()
{
    cx_path_t helloAbsPath;
    cx_str_format(helloAbsPath, sizeof(helloAbsPath), "%s/%s", cwdPath, FILE_NAME);

    cx_file_path(&tmpPath, "%s", FILE_NAME);
    CU_ASSERT_STRING_EQUAL(tmpPath, helloAbsPath);
}

void t_file_should_accept_absolute_paths()
{
    cx_file_path(&tmpPath, "/%s", FILE_NAME);
    CU_ASSERT_STRING_EQUAL(tmpPath, "/" FILE_NAME);
}

void t_file_should_exist()
{
    cx_file_path(&tmpPath, "%s", TEST_FILE_PATH);
    CU_ASSERT_TRUE(cx_file_exists(&tmpPath));

    cx_file_path(&tmpPath, "%s", TEST_FOLDER_PATH);
    CU_ASSERT_TRUE(cx_file_exists(&tmpPath));
}

void t_file_should_not_exist()
{
    cx_file_path(&tmpPath, "%s", "ah_sj2kd6j65o65w6wei.txt");
    CU_ASSERT_FALSE(cx_file_exists(&tmpPath));
}

void t_file_should_get_size()
{
    cx_file_path(&tmpPath, "%s", TEST_FILE_PATH);
    CU_ASSERT(5 == cx_file_get_size(&tmpPath));
}

void t_file_should_get_name()
{
    cx_path_t fileName;
    cx_file_path(&tmpPath, "%s", FILE_NAME);

    cx_file_get_name(&tmpPath, false, &fileName);
    CU_ASSERT_STRING_EQUAL(fileName, FILE_NAME);

    cx_file_get_name(&tmpPath, true, &fileName);
    CU_ASSERT_STRING_EQUAL(fileName, FILE_NAME_NO_EXTENSION);
}

void t_file_should_get_path()
{
    cx_file_path(&tmpPath, "%s", FILE_NAME);

    cx_path_t filePath;
    cx_file_get_path(&tmpPath, &filePath);
    CU_ASSERT_STRING_EQUAL(filePath, cwdPath);
}

void t_file_should_change_extension()
{
    cx_file_path(&tmpPath, "/%s", FILE_NAME);

    cx_file_set_extension(&tmpPath, "html");
    CU_ASSERT_STRING_EQUAL(tmpPath, "/" FILE_NAME_NO_EXTENSION ".html");
}

void t_file_should_append_extension()
{
    cx_file_path(&tmpPath, "/%s", FILE_NAME_NO_EXTENSION);

    cx_file_set_extension(&tmpPath, "html");
    CU_ASSERT_STRING_EQUAL(tmpPath, "/" FILE_NAME_NO_EXTENSION ".html");
}
