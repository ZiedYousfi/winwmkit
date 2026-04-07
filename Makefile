TOOLCHAIN ?= msvc

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
TOOLCHAIN_BUILD_DIR = $(BUILD_DIR)/$(TOOLCHAIN)
LIB_BUILD_DIR = $(TOOLCHAIN_BUILD_DIR)/lib
CLANG_BUILD_DIR = $(BUILD_DIR)/clang
CLANG_LIB_BUILD_DIR = $(CLANG_BUILD_DIR)/lib
CLANGD_DB = compile_commands.json
CLANGD_DIR = $(abspath .)
CLANGD_INCLUDE_DIR = $(abspath $(INCLUDE_DIR))

JSON_ESCAPE = $(subst \,\\,$(subst ",\",$(1)))
JSON_STRING = "$(call JSON_ESCAPE,$(1))"
ROOT_CLANG_OUTPUT = $(abspath $(CLANG_LIB_BUILD_DIR))/$(notdir $(basename $(1))).obj
ROOT_CLANG_ARGS = $(call JSON_STRING,clang-cl.exe), $(call JSON_STRING,/I$(CLANGD_INCLUDE_DIR)), $(call JSON_STRING,/nologo), $(call JSON_STRING,/W4), $(call JSON_STRING,/DWWMK_BUILD_DLL), $(call JSON_STRING,/c), $(call JSON_STRING,$(abspath $(1))), $(call JSON_STRING,/Fo$(call ROOT_CLANG_OUTPUT,$(1)))
CLANGDB_ENTRY = { "directory": $(call JSON_STRING,$(1)), "arguments": [$(2)], "file": $(call JSON_STRING,$(3)), "output": $(call JSON_STRING,$(4)) }
ROOT_CLANGDB_ENTRY = $(call CLANGDB_ENTRY,$(CLANGD_DIR),$(call ROOT_CLANG_ARGS,$(1)),$(abspath $(1)),$(call ROOT_CLANG_OUTPUT,$(1)))
ROOT_CLANGDB_COMMANDS = $(eval __CLANGDB_SEPARATOR:=)$(foreach src,$(LIB_SOURCES),echo $(__CLANGDB_SEPARATOR)$(call ROOT_CLANGDB_ENTRY,$(src)) & $(eval __CLANGDB_SEPARATOR:=,))

ifeq ($(TOOLCHAIN),msvc)
CC = cl.exe
AR = lib.exe
CPPFLAGS = /I$(INCLUDE_DIR)
CFLAGS = /nologo /W4
DLLFLAGS = /DWWMK_BUILD_DLL /LD
else ifeq ($(TOOLCHAIN),clang)
CC = clang-cl.exe
AR = llvm-lib.exe
CPPFLAGS = /I$(INCLUDE_DIR)
CFLAGS = /nologo /W4
DLLFLAGS = /DWWMK_BUILD_DLL /LD
else
$(error Unsupported TOOLCHAIN "$(TOOLCHAIN)". Use TOOLCHAIN=msvc or TOOLCHAIN=clang)
endif

STATIC_LIB = $(LIB_BUILD_DIR)/winwmkit.lib
SHARED_DLL = $(LIB_BUILD_DIR)/winwmkit.dll
IMPORT_LIB = $(LIB_BUILD_DIR)/winwmkit_dll.lib
WIN32_LIBS = user32.lib

LIB_SOURCES = $(wildcard $(SRC_DIR)/*.c)
LIB_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(LIB_BUILD_DIR)/%.obj,$(LIB_SOURCES))

.PHONY: all build static shared clang clang-build clang-static clang-shared clang-db clangd clean

all: build

build: static shared

clang:
	$(MAKE) TOOLCHAIN=clang build
	$(MAKE) TOOLCHAIN=clang clang-db
	$(MAKE) -C exemple TOOLCHAIN=clang clang-db

clang-build:
	$(MAKE) TOOLCHAIN=clang build

clang-static:
	$(MAKE) TOOLCHAIN=clang static

clang-shared:
	$(MAKE) TOOLCHAIN=clang shared

clang-db:
	@(echo [ & $(ROOT_CLANGDB_COMMANDS) echo ]) > "$(CLANGD_DB)"

clangd: clang-db

static: $(STATIC_LIB)

shared: $(SHARED_DLL)

$(LIB_BUILD_DIR):
	if not exist "$(LIB_BUILD_DIR)" mkdir "$(LIB_BUILD_DIR)"

$(LIB_BUILD_DIR)/%.obj: $(SRC_DIR)/%.c | $(LIB_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) /c $< /Fo$@

$(STATIC_LIB): $(LIB_OBJECTS)
	$(AR) /nologo /OUT:$@ $(LIB_OBJECTS)

$(SHARED_DLL): $(LIB_SOURCES) | $(LIB_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DLLFLAGS) $(LIB_SOURCES) /Fe$@ /link $(WIN32_LIBS) /IMPLIB:$(IMPORT_LIB)

clean:
	if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)"
	if exist "$(CLANGD_DB)" del /Q "$(CLANGD_DB)"
	if exist "exemple\compile_commands.json" del /Q "exemple\compile_commands.json"
