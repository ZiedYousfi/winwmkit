SANITIZERS ?=address

ROOT_DIR = ..
INCLUDE_DIR = $(ROOT_DIR)/include
LIB_DIR = $(ROOT_DIR)/build/lib
BUILD_DIR = build

EXAMPLE_OBJ = $(BUILD_DIR)/main.obj
EXAMPLE_EXE = $(BUILD_DIR)/winwmkit_example.exe
STATIC_LIB = $(LIB_DIR)/winwmkit.lib
EXAMPLE_SOURCES = main.c
WIN32_LIBS = user32.lib

ifneq ($(strip $(SANITIZERS)),)
SANITIZER_FLAGS = /fsanitize=address /Zi /Od
CRT_FLAGS = /MD
endif

CC = cl.exe
CPPFLAGS = /I$(INCLUDE_DIR)
CFLAGS = /nologo /W4 $(CRT_FLAGS) $(SANITIZER_FLAGS)

.PHONY: all compdb clangd clean

all: $(EXAMPLE_EXE)

compdb:
	$(MAKE) -C $(ROOT_DIR) compdb SANITIZERS=$(SANITIZERS)

clangd: compdb

$(BUILD_DIR):
	if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

$(STATIC_LIB):
	$(MAKE) -C $(ROOT_DIR) static SANITIZERS=$(SANITIZERS)

$(EXAMPLE_OBJ): main.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) /c $< /Fo$@

$(EXAMPLE_EXE): $(EXAMPLE_OBJ) $(STATIC_LIB)
	$(CC) $(CFLAGS) $(EXAMPLE_OBJ) /Fe$@ /link $(STATIC_LIB) $(WIN32_LIBS)

clean:
	if exist "build" rmdir /S /Q "build"
	if exist "$(ROOT_DIR)\compile_commands.json" del /Q "$(ROOT_DIR)\compile_commands.json"
