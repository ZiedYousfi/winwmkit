#pragma once

#ifdef WWMK_BUILD_DLL
#define WWMK_API __declspec(dllexport)
#elif defined(WWMK_USE_DLL)
#define WWMK_API __declspec(dllimport)
#else
#define WWMK_API
#endif

#include <stdint.h>

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
  uintptr_t id;
  WWMK_Rect rect;
  WWMK_Rect work_rect;
  int is_primary;
} WWMK_Monitor;

typedef struct {
  void *hwnd;
  char title[256];
  WWMK_Rect rect;
  int is_visible;
  int is_minimized;
  int is_maximized;
  /* Points to GUID storage owned by the buffer returned by wwmk_get_windows. */
  void *virtual_desktop;
  int has_virtual_desktop;
} WWMK_Window;

typedef enum {
  WWMK_EVENT_NONE = 0,
  WWMK_EVENT_WINDOW_CREATED,
  WWMK_EVENT_WINDOW_DESTROYED,
  WWMK_EVENT_WINDOW_MOVED,
  WWMK_EVENT_WINDOW_RESIZED,
  WWMK_EVENT_WINDOW_FOCUSED,
  WWMK_EVENT_MONITOR_CHANGED
} WWMK_EventType;

typedef struct {
  WWMK_EventType type;
  uintptr_t window_id;
  uintptr_t monitor_id;
} WWMK_Event;

typedef void (*WWMK_EventCallback)(const WWMK_Event *event, void *userdata);

WWMK_API int wwmk_set_event_callback(WWMK_EventCallback callback,
                                     void *userdata);
WWMK_API int wwmk_start(void);
WWMK_API int wwmk_stop(void);

WWMK_API int wwmk_on_window_created(WWMK_EventCallback callback,
                                    void *userdata);
WWMK_API int wwmk_on_window_destroyed(WWMK_EventCallback callback,
                                      void *userdata);
WWMK_API int wwmk_on_window_moved(WWMK_EventCallback callback, void *userdata);
WWMK_API int wwmk_on_monitor_changed(WWMK_EventCallback callback,
                                     void *userdata);

WWMK_API int wwmk_get_monitors(WWMK_Monitor *out, int cap);
/* Always allocates a new buffer and stores it in *out. Caller frees only *out
 * with free(). Any WWMK_Window.virtual_desktop pointer returned for an entry is
 * owned by that same allocation and must not be freed separately. */
WWMK_API int wwmk_get_windows(WWMK_Window **out, int cap);

WWMK_API int wwmk_get_window_rect(WWMK_Window window, WWMK_Rect *out);
WWMK_API int wwmk_set_window_rect(WWMK_Window window, WWMK_Rect rect);
WWMK_API int wwmk_move_window(WWMK_Window window, int x, int y);
WWMK_API int wwmk_resize_window(WWMK_Window window, int width, int height);

WWMK_API int wwmk_window_is_on_monitor(WWMK_Window window,
                                       WWMK_Monitor monitor);
WWMK_API WWMK_Monitor wwmk_monitor_from_window(WWMK_Window window);
WWMK_API int wwmk_rect_intersects(WWMK_Rect a, WWMK_Rect b);
WWMK_API int wwmk_rect_contains_point(WWMK_Rect rect, WWMK_Point point);
WWMK_API int wwmk_rect_intersection(WWMK_Rect a, WWMK_Rect b, WWMK_Rect *out);
WWMK_API int wwmk_rect_intersection_area(WWMK_Rect a, WWMK_Rect b, int *out);
WWMK_API int wwmk_window_intersects_monitor(WWMK_Window window,
                                            WWMK_Monitor monitor);
WWMK_API int wwmk_window_intersection_area_with_monitor(WWMK_Window window,
                                                        WWMK_Monitor monitor,
                                                        int *out);
WWMK_API int wwmk_window_primary_monitor(WWMK_Window window, WWMK_Monitor *out);
WWMK_API int wwmk_window_monitor_by_center(WWMK_Window window,
                                           WWMK_Monitor *out);
WWMK_API int wwmk_rect_visible_region_on_monitors(WWMK_Rect rect,
                                                  WWMK_Rect *out, int cap);
WWMK_API int wwmk_rect_is_fully_offscreen(WWMK_Rect rect);
WWMK_API int wwmk_rect_is_partially_visible(WWMK_Rect rect);
WWMK_API int wwmk_get_monitor_layout_bounds(WWMK_Rect *out);
WWMK_API int wwmk_get_uncovered_regions(WWMK_Rect *out, int cap);
WWMK_API int wwmk_rect_is_visible_on_any_monitor(WWMK_Rect rect);
WWMK_API int wwmk_get_virtual_space(WWMK_Rect *out);
WWMK_API WWMK_Point wwmk_rect_center(WWMK_Rect rect);

#ifdef __cplusplus
}
#endif
