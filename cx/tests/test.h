#ifndef CX_TESTS_H_
#define CX_TESTS_H_

#include "mem.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <CUnit/CUnit.h>

#define CUNIT_RESULT(success) (success) ? 0 : -1

#endif // CX_TESTS_H_