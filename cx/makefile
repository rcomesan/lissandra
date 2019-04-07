# Project
PROJECT_NAME := cx

# Colored output
ifeq ($(NO_COLOR),)
	COLOR_NONE='\033[0m'
	COLOR_RED='\033[0;31m'
	COLOR_GREEN='\033[0;32m'
	COLOR_WHITE='\033[1;37m'
endif

# Dependencies
LIB_COMMONS := libcommons.so

# Gcc
CC := gcc
CFLAGS ?= -I/usr/include -L/usr/lib -Iinclude/cx/ -g3 -fPIC -Wall -Wextra

# Paths
PATH_SRC   ?= src/
PATH_BUILD ?= build/

# Files
FILES_C := $(shell find $(PATH_SRC) -name *.c)
FILES_H := $(shell find $(PATH_SRC) -name *.h)
FILES_O := $(FILES_C:$(PATH_SRC)%.c=$(PATH_BUILD).obj/%.o)
BIN_OUTPUT_RELEASE := lib$(PROJECT_NAME).so
BIN_OUTPUT_DEBUG := lib$(PROJECT_NAME)d.so

.PHONY: default
default: release

.PHONY: all
all: release

.PHONY: _build
_build: directory $(PATH_BUILD)$(BIN_OUTPUT)

.PHONY: release
release: export PATH_BUILD := $(PATH_BUILD)release/
release: export BIN_OUTPUT := $(BIN_OUTPUT_RELEASE)
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

.PHONY: clean
clean:
	@rm -rf $(PATH_BUILD)

.PHONY: directory
directory:
	@mkdir -p $(PATH_BUILD)
	@mkdir -p $(PATH_BUILD).obj/

$(PATH_BUILD).obj/%.o: $(PATH_SRC)%.c
	$(CC) $(CFLAGS) -c $< -o $@ -l:$(LIB_COMMONS)

$(PATH_BUILD)$(BIN_OUTPUT): $(FILES_O)
	$(CC) -shared $(CFLAGS) $(FILES_O) -o $(PATH_BUILD)$(BIN_OUTPUT) -l:$(LIB_COMMONS)
