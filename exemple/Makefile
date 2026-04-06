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

EXAMPLE_OBJ = $(BUILD_DIR)/main.obj
EXAMPLE_EXE = $(BUILD_DIR)/winwmkit_example.exe
STATIC_LIB = $(LIB_DIR)/winwmkit.lib

.PHONY: all clang clean

all: $(EXAMPLE_EXE)

clang:
	$(MAKE) TOOLCHAIN=clang all

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
