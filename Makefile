CC=cl.exe
AR=lib.exe

CFLAGS=/nologo /W4 /EHsc
OBJ=main.obj

build: static shared

main.obj: main.c
	$(CC) $(CFLAGS) /c main.c /Fo:$(OBJ)

static: $(OBJ)
	@echo Building static library...
	$(AR) /nologo /OUT:winwmkit.lib $(OBJ)

shared:
	@echo Building DLL...
	$(CC) $(CFLAGS) /LD main.c /Fe:winwmkit.dll /link /IMPLIB:winwmkit_dll.lib

clean:
	-del /Q *.obj *.exp *.lib *.dll 2>nul
