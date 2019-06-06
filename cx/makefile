# Project
PROJECT_NAME := cx

# Colored output
ifeq ($(NO_COLOR),)
COLOR_NONE='\033[0m'
COLOR_RED='\033[0;31m'
COLOR_GREEN='\033[0;32m'
COLOR_WHITE='\033[1;37m'
endif

# Gcc
CC := gcc
CFLAGS ?=                                 \
	-I/usr/include                        \
	-L/usr/lib                            \
	-Iinclude/cx/                         \
	-g3 -Wall -Wextra                     \
	-Wno-type-limits                      \
	-Wno-unused-parameter                 \
	-Wno-discarded-qualifiers

# Dependencies
LIBS ?= -l:libcommons.so -l:libreadline.so -l:libpthread.so -l:libm.so

# Build
SHARED_LIB ?= -shared -fPIC

# Paths
PATH_SRC   ?= src/
PATH_TESTS ?= tests/
PATH_BUILD ?= build/

# Files
FILES_C ?= $(shell find $(PATH_SRC) -name *.c)
FILES_O ?= $(FILES_C:$(PATH_SRC)%.c=$(PATH_BUILD).obj/%.o)

FILES_C_TESTS ?= $(shell find $(PATH_TESTS) -name *.c)
FILES_O_TESTS ?= $(FILES_C_TESTS:$(PATH_TESTS)%.c=$(PATH_BUILD).obj/tests/%.o)

BIN_OUTPUT_RELEASE := lib$(PROJECT_NAME).so
BIN_OUTPUT_DEBUG := lib$(PROJECT_NAME)d.so
BIN_OUTPUT_TESTS := $(PROJECT_NAME)-tests.out

.PHONY: default
default: release

.PHONY: all
all: release

.PHONY: _build
_build: directory $(PATH_BUILD)$(BIN_OUTPUT)

.PHONY: release
release: export PATH_BUILD := $(PATH_BUILD)release/
release: export BIN_OUTPUT := $(BIN_OUTPUT_RELEASE)
release: export CFLAGS := $(CFLAGS) -DCX_RELEASE_VERBOSE -ggdb3
release:
	@echo ""
	@echo ${COLOR_WHITE}"** [$(BIN_OUTPUT)] Building RELEASE..."${COLOR_NONE}
	@echo ""
	@$(MAKE) _build --no-print-directory
	@echo ""
	@echo ${COLOR_GREEN}"** [$(BIN_OUTPUT)] Release build completed successfully."${COLOR_NONE}
	@echo ""

.PHONY: debug
debug: export PATH_BUILD := $(PATH_BUILD)debug/
debug: export BIN_OUTPUT := $(BIN_OUTPUT_DEBUG)
debug: export CFLAGS := $(CFLAGS) -DDEBUG -ggdb3
debug:
	@echo ""
	@echo ${COLOR_WHITE}"** [$(BIN_OUTPUT)] Building DEBUG..."${COLOR_NONE}
	@echo ""
	@$(MAKE) _build --no-print-directory
	@echo ""
	@echo ${COLOR_GREEN}"** [$(BIN_OUTPUT)] Debug build completed successfully."${COLOR_NONE}
	@echo ""

.PHONY: tests
tests: export PATH_BUILD := $(PATH_BUILD)tests/
tests: export BIN_OUTPUT := $(BIN_OUTPUT_TESTS)
tests: export LIBS := $(LIBS) -lcunit
tests: export SHARED_LIB := 
tests: export TESTS := $(PATH_TESTS)test.o
tests:
	@echo ""
	@echo ${COLOR_WHITE}"** [$(BIN_OUTPUT)] Building TESTS..."${COLOR_NONE}
	@echo ""
	@$(MAKE) _build --no-print-directory
	@echo ""
	@echo ${COLOR_GREEN}"** [$(BIN_OUTPUT)] Tests build completed successfully."${COLOR_NONE}
	@echo ""

.PHONY: runtests
runtests: export PATH_BUILD := $(PATH_BUILD)tests/
runtests: export BIN_OUTPUT := $(BIN_OUTPUT_TESTS)
runtests: tests
	@echo ${COLOR_WHITE}"** [$(BIN_OUTPUT)] Running tests..."${COLOR_WHITE}
	@echo ""
	@./$(PATH_BUILD)$(BIN_OUTPUT)
	@echo ""
	@echo ${COLOR_GREEN}"** [$(BIN_OUTPUT)] Tests OK!"${COLOR_NONE}
	@echo ""

.PHONY: valgrind
valgrind: export PATH_BUILD := $(PATH_BUILD)tests/
valgrind: export BIN_OUTPUT := $(BIN_OUTPUT_TESTS)
valgrind: runtests
	@valgrind --leak-check=full --track-origins=yes --error-exitcode=1 -v $(PATH_BUILD)$(BIN_OUTPUT)
	@echo ""
	@echo ${COLOR_GREEN}"** [$(BIN_OUTPUT)] Valgrind checks passed successfully."${COLOR_NONE}
	@echo ""

.PHONY: clean
clean:
	@rm -rf $(PATH_BUILD)

.PHONY: directory
directory:
	@mkdir -p $(PATH_BUILD)
	@mkdir -p $(PATH_BUILD).obj/
	@mkdir -p $(PATH_BUILD).obj/tests/

$(PATH_BUILD).obj/tests/test.o: $(FILES_C_TESTS)
	$(CC) $(CFLAGS) -c $< -o $@

$(PATH_BUILD).obj/%.o: $(PATH_SRC)%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(PATH_BUILD)$(BIN_OUTPUT): $(FILES_O) $(TESTS)
	$(CC) $(CFLAGS) $(SHARED_LIB) -o $(PATH_BUILD)$(BIN_OUTPUT) $(FILES_O) $(TESTS) $(LIBS)
