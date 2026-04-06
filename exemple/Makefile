TOOLCHAIN ?= msvc

ifeq ($(TOOLCHAIN),msvc)
CC = cl.exe
CPPFLAGS = /I$(INCLUDE_DIR)
CFLAGS = /nologo /W4
else ifeq ($(TOOLCHAIN),clang)
CC = clang-cl.exe
CPPFLAGS = /I$(INCLUDE_DIR)
CFLAGS = /nologo /W4
else
$(error Unsupported TOOLCHAIN "$(TOOLCHAIN)". Use TOOLCHAIN=msvc or TOOLCHAIN=clang)
endif

ROOT_DIR = ..
INCLUDE_DIR = $(ROOT_DIR)/include
LIB_DIR = $(ROOT_DIR)/build/$(TOOLCHAIN)/lib
BUILD_DIR = build/$(TOOLCHAIN)
CLANG_BUILD_DIR = build/clang
CLANGD_DB = compile_commands.json
CLANGD_DIR = $(abspath .)
CLANGD_INCLUDE_DIR = $(abspath $(INCLUDE_DIR))

EXAMPLE_OBJ = $(BUILD_DIR)/main.obj
EXAMPLE_EXE = $(BUILD_DIR)/winwmkit_example.exe
STATIC_LIB = $(LIB_DIR)/winwmkit.lib
EXAMPLE_SOURCES = main.c

JSON_ESCAPE = $(subst \,\\,$(subst ",\",$(1)))
JSON_STRING = "$(call JSON_ESCAPE,$(1))"
EXAMPLE_CLANG_OUTPUT = $(abspath $(CLANG_BUILD_DIR))/$(notdir $(basename $(1))).obj
EXAMPLE_CLANG_ARGS = $(call JSON_STRING,clang-cl.exe), $(call JSON_STRING,/I$(CLANGD_INCLUDE_DIR)), $(call JSON_STRING,/nologo), $(call JSON_STRING,/W4), $(call JSON_STRING,/c), $(call JSON_STRING,$(abspath $(1))), $(call JSON_STRING,/Fo$(call EXAMPLE_CLANG_OUTPUT,$(1)))
CLANGDB_ENTRY = { "directory": $(call JSON_STRING,$(1)), "arguments": [$(2)], "file": $(call JSON_STRING,$(3)), "output": $(call JSON_STRING,$(4)) }
EXAMPLE_CLANGDB_ENTRY = $(call CLANGDB_ENTRY,$(CLANGD_DIR),$(call EXAMPLE_CLANG_ARGS,$(1)),$(abspath $(1)),$(call EXAMPLE_CLANG_OUTPUT,$(1)))
EXAMPLE_CLANGDB_COMMANDS = $(eval __CLANGDB_SEPARATOR:=)$(foreach src,$(EXAMPLE_SOURCES),echo $(__CLANGDB_SEPARATOR)$(call EXAMPLE_CLANGDB_ENTRY,$(src)) & $(eval __CLANGDB_SEPARATOR:=,))

.PHONY: all clang clang-db clangd clean

all: $(EXAMPLE_EXE)

clang:
	$(MAKE) TOOLCHAIN=clang all
	$(MAKE) TOOLCHAIN=clang clang-db
	$(MAKE) -C $(ROOT_DIR) TOOLCHAIN=clang clang-db

clang-db:
	@(echo [ & $(EXAMPLE_CLANGDB_COMMANDS) echo ]) > "$(CLANGD_DB)"

clangd: clang-db

$(BUILD_DIR):
	if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

$(STATIC_LIB):
	$(MAKE) -C $(ROOT_DIR) TOOLCHAIN=$(TOOLCHAIN) static

$(EXAMPLE_OBJ): main.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) /c $< /Fo$@

$(EXAMPLE_EXE): $(EXAMPLE_OBJ) $(STATIC_LIB)
	$(CC) $(CFLAGS) $(EXAMPLE_OBJ) /Fe$@ /link /LIBPATH:$(LIB_DIR) winwmkit.lib

clean:
	if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)"
	if exist "$(CLANGD_DB)" del /Q "$(CLANGD_DB)"
