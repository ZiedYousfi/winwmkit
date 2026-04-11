SANITIZERS ?= address

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
LIB_BUILD_DIR = $(BUILD_DIR)/lib
COMPDB = compile_commands.json

ifneq ($(strip $(SANITIZERS)),)
SANITIZER_FLAGS = /fsanitize=address /Zi /Od
CRT_FLAGS = /MD
endif

CC = cl.exe
AR = lib.exe
CPPFLAGS = /I$(INCLUDE_DIR)
CFLAGS = /nologo /W4 $(CRT_FLAGS) $(SANITIZER_FLAGS)
DLLFLAGS = /DWWMK_BUILD_DLL /LD
TARGET_ARCH = $(if $(VSCMD_ARG_TGT_ARCH),$(VSCMD_ARG_TGT_ARCH),unknown)
ARCH_STAMP = $(LIB_BUILD_DIR)/.arch-$(TARGET_ARCH).stamp

STATIC_LIB = $(LIB_BUILD_DIR)/winwmkit.lib
SHARED_DLL = $(LIB_BUILD_DIR)/winwmkit.dll
IMPORT_LIB = $(LIB_BUILD_DIR)/winwmkit_dll.lib
WIN32_LIBS = user32.lib kernel32.lib ole32.lib

LIB_SOURCES = $(wildcard $(SRC_DIR)/*.c)
LIB_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(LIB_BUILD_DIR)/%.obj,$(LIB_SOURCES))

.PHONY: all build static shared compdb clangd clean

all: build compdb

build: static shared

compdb:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/generate_compile_commands.ps1 -Sanitizers "$(SANITIZERS)"

clangd: compdb

static: $(STATIC_LIB)

shared: $(SHARED_DLL)

$(LIB_BUILD_DIR):
	if not exist "$(LIB_BUILD_DIR)" mkdir "$(LIB_BUILD_DIR)"

$(ARCH_STAMP): | $(LIB_BUILD_DIR)
	del /Q "$(LIB_BUILD_DIR)\\*.obj" 2>nul || exit 0
	del /Q "$(LIB_BUILD_DIR)\\*.lib" 2>nul || exit 0
	del /Q "$(LIB_BUILD_DIR)\\*.exp" 2>nul || exit 0
	del /Q "$(LIB_BUILD_DIR)\\*.dll" 2>nul || exit 0
	del /Q "$(LIB_BUILD_DIR)\\*.pdb" 2>nul || exit 0
	powershell -NoProfile -Command "Set-Content -Path '$(ARCH_STAMP)' -Value '$(TARGET_ARCH)'"

$(LIB_BUILD_DIR)/%.obj: $(SRC_DIR)/%.c Makefile $(ARCH_STAMP) | $(LIB_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) /c $< /Fo$@

$(STATIC_LIB): $(LIB_OBJECTS)
	$(AR) /nologo /OUT:$@ $(LIB_OBJECTS)

$(SHARED_DLL): $(LIB_SOURCES) | $(LIB_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DLLFLAGS) $(LIB_SOURCES) /Fe$@ /link $(WIN32_LIBS) /IMPLIB:$(IMPORT_LIB)

clean:
	if exist "build" rmdir /S /Q "build"
	if exist "$(COMPDB)" del /Q "$(COMPDB)"
