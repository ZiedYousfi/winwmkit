#pragma once

#ifdef WWMK_BUILD_DLL
#define WWMK_API __declspec(dllexport)
#elif defined(WWMK_USE_DLL)
#define WWMK_API __declspec(dllimport)
#else
#define WWMK_API
#endif

#include <stdbool.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int x;
  int y;
} WWMK_Point;

typedef struct {
  int width;
  int height;
} WWMK_Size;

typedef struct {
  int x;
  int y;
  int width;
  int height;
} WWMK_Rect;

typedef struct {
  void *handle;
  WWMK_Rect rect;
  WWMK_Rect work_rect;
  int is_primary;
} WWMK_Monitor;

typedef struct {
  void *handle;
  WWMK_Rect rect;
  int is_visible;
  int is_minimized;
  int is_maximized;
} WWMK_Window;

typedef enum {
  WWMK_EVENT_NONE,
  WWMK_EVENT_WINDOW_CREATED,
  WWMK_EVENT_WINDOW_DESTROYED,
  WWMK_EVENT_WINDOW_MOVED,
  WWMK_EVENT_WINDOW_RESIZED,
  WWMK_EVENT_WINDOW_FOCUSED,
  WWMK_EVENT_MONITOR_CHANGED
} WWMK_EventType;

typedef struct {
  WWMK_EventType type;
  void *window_handle;
  void *monitor_handle;
} WWMK_Event;

#ifdef __cplusplus
} // extern "C"
#endif
