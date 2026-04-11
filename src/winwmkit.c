#include "winwmkit/winwmkit.h"

#include <ShObjIdl.h>
#include <stddef.h>
#include <stdlib.h>
#include <windows.h>

#define WWMK_ALIGNOF(type)                                                     \
  offsetof(                                                                    \
      struct {                                                                 \
        char c;                                                                \
        type value;                                                            \
      },                                                                       \
      value)

static void wwmk_todo_abort(void) { abort(); }

static int wwmk_todo_int(void) {
  wwmk_todo_abort();
  return 0;
}

static WWMK_Monitor wwmk_todo_monitor(void) {
  WWMK_Monitor value = {0};
  wwmk_todo_abort();
  return value;
}

static int wwmk_rect_right(WWMK_Rect rect) { return rect.x + rect.width; }

static int wwmk_rect_bottom(WWMK_Rect rect) { return rect.y + rect.height; }

static int wwmk_rect_intersection_internal(WWMK_Rect a, WWMK_Rect b,
                                           WWMK_Rect *out) {
  int left = a.x > b.x ? a.x : b.x;
  int top = a.y > b.y ? a.y : b.y;
  int right = wwmk_rect_right(a) < wwmk_rect_right(b) ? wwmk_rect_right(a)
                                                      : wwmk_rect_right(b);
  int bottom = wwmk_rect_bottom(a) < wwmk_rect_bottom(b) ? wwmk_rect_bottom(a)
                                                         : wwmk_rect_bottom(b);

  if (out == NULL) {
    return -1;
  }

  if (right <= left || bottom <= top) {
    *out = (WWMK_Rect){0};
    return 0;
  }

  out->x = left;
  out->y = top;
  out->width = right - left;
  out->height = bottom - top;
  return 1;
}

enum {
  WWMK_GET_WINDOWS_INVALID_ARGUMENT = -1,
  WWMK_GET_WINDOWS_OUT_OF_MEMORY = -2,
  WWMK_GET_WINDOWS_ENUM_FAILED = -3
};

int wwmk_set_event_callback(WWMK_EventCallback callback, void *userdata) {
  (void)callback;
  (void)userdata;
  return wwmk_todo_int();
}

int wwmk_start(void) { return wwmk_todo_int(); }

int wwmk_stop(void) { return wwmk_todo_int(); }

int wwmk_on_window_created(WWMK_EventCallback callback, void *userdata) {
  (void)callback;
  (void)userdata;
  return wwmk_todo_int();
}

int wwmk_on_window_destroyed(WWMK_EventCallback callback, void *userdata) {
  (void)callback;
  (void)userdata;
  return wwmk_todo_int();
}

int wwmk_on_window_moved(WWMK_EventCallback callback, void *userdata) {
  (void)callback;
  (void)userdata;
  return wwmk_todo_int();
}

int wwmk_on_monitor_changed(WWMK_EventCallback callback, void *userdata) {
  (void)callback;
  (void)userdata;
  return wwmk_todo_int();
}

int wwmk_get_monitors(WWMK_Monitor *out, int cap) {
  (void)out;
  (void)cap;
  return wwmk_todo_int();
}

struct EnumWindowsCallbackLParam {
  WWMK_Window *buffer;
  GUID *virtual_desktop_ids;
  int count;
  int cap;
  int status;
  IVirtualDesktopManager *virtual_desktop_manager;
};

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
  struct EnumWindowsCallbackLParam *ctx =
      (struct EnumWindowsCallbackLParam *)lParam;
  WWMK_Window *grown_windows = NULL;
  GUID *grown_virtual_desktops = NULL;
  WWMK_Window window = {0};
  RECT rect = {0};
  int next_capacity = 0;

  if (ctx->count >= ctx->cap) {
    next_capacity = ctx->cap > 0 ? ctx->cap * 2 : 16;

    grown_windows =
        realloc(ctx->buffer, sizeof(*grown_windows) * (size_t)next_capacity);
    if (grown_windows == NULL) {
      ctx->status = WWMK_GET_WINDOWS_OUT_OF_MEMORY;
      return FALSE;
    }
    ctx->buffer = grown_windows;

    grown_virtual_desktops =
        realloc(ctx->virtual_desktop_ids,
                sizeof(*grown_virtual_desktops) * (size_t)next_capacity);
    if (grown_virtual_desktops == NULL) {
      ctx->status = WWMK_GET_WINDOWS_OUT_OF_MEMORY;
      return FALSE;
    }

    ctx->virtual_desktop_ids = grown_virtual_desktops;
    ctx->cap = next_capacity;
  }

  window.hwnd = hwnd;
  GetWindowTextA(hwnd, window.title, 256);
  window.is_visible = IsWindowVisible(hwnd) ? 1 : 0;
  window.is_minimized = IsIconic(hwnd) ? 1 : 0;
  window.is_maximized = IsZoomed(hwnd) ? 1 : 0;
  (void)rect;
  (void)wwmk_get_window_rect(window, &window.rect);

  if (ctx->virtual_desktop_manager != NULL) {
    GUID desktop_id = {0};
    HRESULT hr = ctx->virtual_desktop_manager->lpVtbl->GetWindowDesktopId(
        ctx->virtual_desktop_manager, hwnd, &desktop_id);
    if (SUCCEEDED(hr)) {
      ctx->virtual_desktop_ids[ctx->count] = desktop_id;
      window.has_virtual_desktop = 1;
    }
  }

  ctx->buffer[ctx->count] = window;
  ctx->count++;

  return TRUE;
}

int wwmk_get_windows(WWMK_Window **out, int cap) {
  struct EnumWindowsCallbackLParam result = {0};
  char *allocation = NULL;
  WWMK_Window *windows = NULL;
  GUID *virtual_desktops = NULL;
  HRESULT com_hr = S_OK;
  int initialCap = cap;
  int i = 0;
  size_t windows_size = 0;
  size_t virtual_desktops_offset = 0;
  size_t virtual_desktops_size = 0;
  size_t total_size = 0;

  if (out == NULL || cap < 0) {
    return WWMK_GET_WINDOWS_INVALID_ARGUMENT;
  }

  *out = NULL;

  if (initialCap == 0) {
    initialCap = 16;
  }

  result.buffer = malloc(sizeof(*result.buffer) * (size_t)initialCap);
  if (result.buffer == NULL) {
    return WWMK_GET_WINDOWS_OUT_OF_MEMORY;
  }
  result.virtual_desktop_ids =
      malloc(sizeof(*result.virtual_desktop_ids) * (size_t)initialCap);
  if (result.virtual_desktop_ids == NULL) {
    free(result.buffer);
    return WWMK_GET_WINDOWS_OUT_OF_MEMORY;
  }
  result.count = 0;
  result.cap = initialCap;
  result.status = 0;
  result.virtual_desktop_manager = NULL;

  com_hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  if (com_hr == RPC_E_CHANGED_MODE) {
    com_hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  }
  if (SUCCEEDED(com_hr) || com_hr == RPC_E_CHANGED_MODE) {
    (void)CoCreateInstance(&CLSID_VirtualDesktopManager, NULL,
                           CLSCTX_INPROC_SERVER, &IID_IVirtualDesktopManager,
                           (void **)&result.virtual_desktop_manager);
  }

  if (!EnumWindows(EnumWindowsCallback, (LPARAM)&result)) {
    if (result.virtual_desktop_manager != NULL) {
      result.virtual_desktop_manager->lpVtbl->Release(
          result.virtual_desktop_manager);
    }
    if (com_hr == S_OK || com_hr == S_FALSE) {
      CoUninitialize();
    }
    if (result.status < 0) {
      free(result.buffer);
      free(result.virtual_desktop_ids);
      return result.status;
    }
    free(result.buffer);
    free(result.virtual_desktop_ids);
    return WWMK_GET_WINDOWS_ENUM_FAILED;
  }

  if (result.virtual_desktop_manager != NULL) {
    result.virtual_desktop_manager->lpVtbl->Release(
        result.virtual_desktop_manager);
  }
  if (com_hr == S_OK || com_hr == S_FALSE) {
    CoUninitialize();
  }

  if (result.count == 0) {
    free(result.buffer);
    free(result.virtual_desktop_ids);
    return 0;
  }

  windows_size = sizeof(*windows) * (size_t)result.count;
  virtual_desktops_offset =
      (windows_size + WWMK_ALIGNOF(GUID) - 1u) & ~(WWMK_ALIGNOF(GUID) - 1u);
  virtual_desktops_size = sizeof(*virtual_desktops) * (size_t)result.count;
  total_size = virtual_desktops_offset + virtual_desktops_size;

  allocation = malloc(total_size);
  if (allocation == NULL) {
    free(result.buffer);
    free(result.virtual_desktop_ids);
    return WWMK_GET_WINDOWS_OUT_OF_MEMORY;
  }

  windows = (WWMK_Window *)allocation;
  virtual_desktops = (GUID *)(allocation + virtual_desktops_offset);

  for (i = 0; i < result.count; i++) {
    windows[i] = result.buffer[i];
    if (windows[i].has_virtual_desktop) {
      virtual_desktops[i] = result.virtual_desktop_ids[i];
      windows[i].virtual_desktop = &virtual_desktops[i];
    } else {
      windows[i].virtual_desktop = NULL;
    }
  }

  *out = windows;
  free(result.buffer);
  free(result.virtual_desktop_ids);
  return result.count;
}

int wwmk_get_window_rect(WWMK_Window window, WWMK_Rect *out) {
  RECT rect = {0};

  if (window.hwnd == NULL || out == NULL) {
    return -1;
  }

  if (!GetWindowRect((HWND)window.hwnd, &rect)) {
    return -1;
  }

  out->x = rect.left;
  out->y = rect.top;
  out->width = rect.right - rect.left;
  out->height = rect.bottom - rect.top;
  return 0;
}

int wwmk_set_window_rect(WWMK_Window window, WWMK_Rect rect) {
  (void)window;
  (void)rect;
  return wwmk_todo_int();
}

int wwmk_move_window(WWMK_Window window, int x, int y) {
  (void)window;
  (void)x;
  (void)y;
  return wwmk_todo_int();
}

int wwmk_resize_window(WWMK_Window window, int width, int height) {
  (void)window;
  (void)width;
  (void)height;
  return wwmk_todo_int();
}

int wwmk_window_is_on_monitor(WWMK_Window window, WWMK_Monitor monitor) {
  return wwmk_rect_intersects(window.rect, monitor.rect);
}

WWMK_Monitor wwmk_monitor_from_window(WWMK_Window window) {
  (void)window;
  return wwmk_todo_monitor();
}

int wwmk_rect_intersects(WWMK_Rect a, WWMK_Rect b) {
  WWMK_Rect intersection = {0};
  return wwmk_rect_intersection_internal(a, b, &intersection);
}

int wwmk_rect_contains_point(WWMK_Rect rect, WWMK_Point point) {
  if (rect.width <= 0 || rect.height <= 0) {
    return 0;
  }

  return point.x >= rect.x && point.x < wwmk_rect_right(rect) &&
         point.y >= rect.y && point.y < wwmk_rect_bottom(rect);
}

int wwmk_rect_intersection(WWMK_Rect a, WWMK_Rect b, WWMK_Rect *out) {
  return wwmk_rect_intersection_internal(a, b, out);
}

int wwmk_rect_intersection_area(WWMK_Rect a, WWMK_Rect b, int *out) {
  WWMK_Rect intersection = {0};
  int status = 0;

  if (out == NULL) {
    return -1;
  }

  status = wwmk_rect_intersection_internal(a, b, &intersection);
  if (status < 0) {
    return status;
  }

  *out = intersection.width * intersection.height;
  return status;
}

int wwmk_window_intersects_monitor(WWMK_Window window, WWMK_Monitor monitor) {
  return wwmk_rect_intersects(window.rect, monitor.rect);
}

int wwmk_window_intersection_area_with_monitor(WWMK_Window window,
                                               WWMK_Monitor monitor, int *out) {
  return wwmk_rect_intersection_area(window.rect, monitor.rect, out);
}

int wwmk_window_primary_monitor(WWMK_Window window, WWMK_Monitor *out) {
  (void)window;
  (void)out;
  return wwmk_todo_int();
}

int wwmk_window_monitor_by_center(WWMK_Window window, WWMK_Monitor *out) {
  (void)window;
  (void)out;
  return wwmk_todo_int();
}

int wwmk_rect_visible_region_on_monitors(WWMK_Rect rect, WWMK_Rect *out,
                                         int cap) {
  (void)rect;
  (void)out;
  (void)cap;
  return wwmk_todo_int();
}

int wwmk_rect_is_fully_offscreen(WWMK_Rect rect) {
  (void)rect;
  return wwmk_todo_int();
}

int wwmk_rect_is_partially_visible(WWMK_Rect rect) {
  (void)rect;
  return wwmk_todo_int();
}

int wwmk_get_monitor_layout_bounds(WWMK_Rect *out) {
  (void)out;
  return wwmk_todo_int();
}

int wwmk_get_uncovered_regions(WWMK_Rect *out, int cap) {
  (void)out;
  (void)cap;
  return wwmk_todo_int();
}

int wwmk_rect_is_visible_on_any_monitor(WWMK_Rect rect) {
  (void)rect;
  return wwmk_todo_int();
}

int wwmk_get_virtual_space(WWMK_Rect *out) {
  if (out == NULL) {
    return -1;
  }

  out->x = GetSystemMetrics(SM_XVIRTUALSCREEN);
  out->y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  out->width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  out->height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  return 0;
}

WWMK_Point wwmk_rect_center(WWMK_Rect rect) {
  WWMK_Point value = {0};

  value.x = rect.x + rect.width / 2;
  value.y = rect.y + rect.height / 2;
  return value;
}
