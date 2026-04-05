CC = cl.exe
AR = lib.exe

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
LIB_BUILD_DIR = $(BUILD_DIR)/lib

CPPFLAGS = /I$(INCLUDE_DIR)
CFLAGS = /nologo /W4 /EHsc

STATIC_LIB = $(LIB_BUILD_DIR)/winwmkit.lib
SHARED_DLL = $(LIB_BUILD_DIR)/winwmkit.dll
IMPORT_LIB = $(LIB_BUILD_DIR)/winwmkit_dll.lib

LIB_SOURCES = $(wildcard $(SRC_DIR)/*.c)
LIB_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(LIB_BUILD_DIR)/%.obj,$(LIB_SOURCES))

ifeq ($(strip $(LIB_SOURCES)),)
$(error No C sources found in $(SRC_DIR). Add library implementation files before building.)
endif

.PHONY: all build static shared clean

all: build

build: static shared

static: $(STATIC_LIB)

shared: $(SHARED_DLL)

$(LIB_BUILD_DIR):
	if not exist "$(LIB_BUILD_DIR)" mkdir "$(LIB_BUILD_DIR)"

$(LIB_BUILD_DIR)/%.obj: $(SRC_DIR)/%.c | $(LIB_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) /c $< /Fo$@

$(STATIC_LIB): $(LIB_OBJECTS)
	$(AR) /nologo /OUT:$@ $(LIB_OBJECTS)

$(SHARED_DLL): $(LIB_SOURCES) | $(LIB_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) /DWWMK_BUILD_DLL /LD $(LIB_SOURCES) /Fe$@ /link /IMPLIB:$(IMPORT_LIB)

clean:
	if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)"
