# Project
PROJECT_NAME := mem
PROJECT_NAME_UCASE := $(shell echo $(PROJECT_NAME) | tr a-z A-Z)

# Colored output
ifeq ($(NO_COLOR),)
COLOR_NONE='\033[0m'
COLOR_RED='\033[0;31m'
COLOR_GREEN='\033[0;32m'
COLOR_WHITE='\033[1;37m'
endif

# Dependencies
LIB_COMMONS := libcommons.so
LIB_CX_RELEASE := libcx.so
LIB_CX_DEBUG := libcxd.so

# Gcc
CC := gcc
CFLAGS ?=                                 \
	-I../cx/include/                      \
	-L../cx/build/$(TARGET)               \
	-I../ker/include/                     \
	-I../mem/include/                     \
	-I../lfs/include/                     \
	-DPROJECT_NAME=\"$(PROJECT_NAME)\"    \
	-D$(PROJECT_NAME_UCASE)               \
	-g3 -Wall -Wextra                     \
	-Wno-unused-variable                  \
	-Wno-unused-parameter

LDFLAGS ?= -Wl,-R -Wl,/home/utnso/lissandra/cx/build/$(TARGET)

# Paths
PATH_SRC        ?= src/
PATH_BUILD      ?= build/
PATH_COMMON_KER ?= ../ker/src/common/
PATH_COMMON_MEM ?= ../mem/src/common/
PATH_COMMON_LFS ?= ../lfs/src/common/

# Files
FILES_C_SRC         := $(shell find $(PATH_SRC) -name *.c)
FILES_O_SRC         := $(FILES_C_SRC:$(PATH_SRC)%.c=$(PATH_BUILD).obj/%.o)

FILES_C_COMMON_KER  := $(shell find $(PATH_COMMON_KER) -name *.c)
FILES_O_COMMON_KER  := $(FILES_C_COMMON_KER:$(PATH_COMMON_KER)%.c=$(PATH_BUILD).obj/common/ker/%.o)

FILES_C_COMMON_LFS  := $(shell find $(PATH_COMMON_LFS) -name *.c)
FILES_O_COMMON_LFS  := $(FILES_C_COMMON_LFS:$(PATH_COMMON_LFS)%.c=$(PATH_BUILD).obj/common/lfs/%.o)

FILES_O             := $(FILES_O_SRC) $(FILES_O_COMMON_LFS) $(FILES_O_COMMON_KER)
BIN_OUTPUT          := $(PROJECT_NAME).out

.PHONY: default
default: release

.PHONY: all
all: release

.PHONY: _build
_build: directory resource dependency $(PATH_BUILD)$(BIN_OUTPUT)

.PHONY: release
release: export TARGET := release
release: export PATH_BUILD := $(PATH_BUILD)$(TARGET)/
release: export LIB_CX := $(LIB_CX_RELEASE)
release:
	@echo ""
	@echo ${COLOR_WHITE}"** [$(BIN_OUTPUT)] Building RELEASE..."${COLOR_NONE}
	@echo ""
	@$(MAKE) _build --no-print-directory
	@echo ""
	@echo ${COLOR_GREEN}"** [$(BIN_OUTPUT)] Release build completed successfully."${COLOR_NONE}
	@echo ""

.PHONY: debug
debug: export TARGET := debug
debug: export PATH_BUILD := $(PATH_BUILD)$(TARGET)/
debug: export LIB_CX := $(LIB_CX_DEBUG)
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
	@mkdir -p $(PATH_BUILD).obj/common/ker/
	@mkdir -p $(PATH_BUILD).obj/common/lfs/
	@mkdir -p $(PATH_BUILD).obj/common/mem/

.PHONY: resource
resource:
	@cp -r res/ $(PATH_BUILD)res

.PHONY: valgrind
valgrind: debug
	@valgrind --leak-check=full --track-origins=yes --error-exitcode=1 -v $(PATH_BUILD)debug/$(BIN_OUTPUT)
	@echo ""
	@echo ${COLOR_GREEN}"** [$(BIN_OUTPUT)] Valgrind checks passed successfully."${COLOR_NONE}
	@echo ""

$(PATH_BUILD).obj/common/ker/%.o: $(PATH_COMMON_KER)%.c
	$(CC) $(CFLAGS) -c $< -o $@ -l:$(LIB_COMMONS) -l:$(LIB_CX)

$(PATH_BUILD).obj/common/lfs/%.o: $(PATH_COMMON_LFS)%.c
	$(CC) $(CFLAGS) -c $< -o $@ -l:$(LIB_COMMONS) -l:$(LIB_CX)

$(PATH_BUILD).obj/%.o: $(PATH_SRC)%.c
	$(CC) $(CFLAGS) -c $< -o $@ -l:$(LIB_COMMONS) -l:$(LIB_CX)

$(PATH_BUILD)$(BIN_OUTPUT): $(FILES_O)
	$(CC) $(CFLAGS) $(FILES_O) -o $(PATH_BUILD)$(BIN_OUTPUT) $(LDFLAGS) -l:$(LIB_COMMONS) -l:$(LIB_CX)

.PHONY: dependency
dependency:
	@:                   \
	&& unset CFLAGS      \
	&& unset PATH_SRC    \
	&& unset PATH_BUILD  \
	&& $(MAKE) $(TARGET) --directory=../cx/ --no-print-directory
