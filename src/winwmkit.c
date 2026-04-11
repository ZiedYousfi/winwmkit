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

enum {
  WWMK_STATUS_INVALID_ARGUMENT = -1,
  WWMK_STATUS_OUT_OF_MEMORY = -2,
  WWMK_STATUS_ENUM_FAILED = -3,
  WWMK_STATUS_NOT_FOUND = -4
};

static WWMK_Rect wwmk_rect_from_win32(const RECT *rect) {
  WWMK_Rect value = {0};

  if (rect == NULL) {
    return value;
  }

  value.x = rect->left;
  value.y = rect->top;
  value.width = rect->right - rect->left;
  value.height = rect->bottom - rect->top;
  return value;
}

static long long wwmk_rect_area_ll(WWMK_Rect rect) {
  if (rect.width <= 0 || rect.height <= 0) {
    return 0;
  }

  return (long long)rect.width * (long long)rect.height;
}

static int wwmk_fill_monitor_from_handle(HMONITOR hmonitor, WWMK_Monitor *out) {
  MONITORINFO info = {0};

  if (hmonitor == NULL || out == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  info.cbSize = sizeof(info);
  if (!GetMonitorInfoA(hmonitor, &info)) {
    return WWMK_STATUS_ENUM_FAILED;
  }

  out->id = (uintptr_t)hmonitor;
  out->rect = wwmk_rect_from_win32(&info.rcMonitor);
  out->work_rect = wwmk_rect_from_win32(&info.rcWork);
  out->is_primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0 ? 1 : 0;
  return 0;
}

struct EnumMonitorsCallbackLParam {
  WWMK_Monitor *buffer;
  int count;
  int cap;
  int status;
};

static BOOL CALLBACK EnumMonitorsCallback(HMONITOR hmonitor, HDC hdc,
                                          LPRECT lprcMonitor, LPARAM lParam) {
  struct EnumMonitorsCallbackLParam *ctx =
      (struct EnumMonitorsCallbackLParam *)lParam;
  WWMK_Monitor *grown_buffer = NULL;

  (void)hdc;
  (void)lprcMonitor;

  if (ctx->count >= ctx->cap) {
    int next_capacity = ctx->cap > 0 ? ctx->cap * 2 : 4;

    grown_buffer =
        realloc(ctx->buffer, sizeof(*grown_buffer) * (size_t)next_capacity);
    if (grown_buffer == NULL) {
      ctx->status = WWMK_STATUS_OUT_OF_MEMORY;
      return FALSE;
    }

    ctx->buffer = grown_buffer;
    ctx->cap = next_capacity;
  }

  ctx->status = wwmk_fill_monitor_from_handle(hmonitor, &ctx->buffer[ctx->count]);
  if (ctx->status < 0) {
    return FALSE;
  }

  ctx->count++;
  return TRUE;
}

static int wwmk_collect_monitors(WWMK_Monitor **out, int *count) {
  struct EnumMonitorsCallbackLParam result = {0};

  if (out == NULL || count == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  *out = NULL;
  *count = 0;

  if (!EnumDisplayMonitors(NULL, NULL, EnumMonitorsCallback, (LPARAM)&result)) {
    free(result.buffer);
    if (result.status < 0) {
      return result.status;
    }
    return WWMK_STATUS_ENUM_FAILED;
  }

  *out = result.buffer;
  *count = result.count;
  return 0;
}

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
  WWMK_Monitor *monitors = NULL;
  int count = 0;
  int status = 0;
  int i = 0;
  int copy_count = 0;

  if (cap < 0 || (cap > 0 && out == NULL)) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  status = wwmk_collect_monitors(&monitors, &count);
  if (status < 0) {
    return status;
  }

  copy_count = count < cap ? count : cap;
  for (i = 0; i < copy_count; i++) {
    out[i] = monitors[i];
  }

  free(monitors);
  return count;
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
      ctx->status = WWMK_STATUS_OUT_OF_MEMORY;
      return FALSE;
    }
    ctx->buffer = grown_windows;

    grown_virtual_desktops =
        realloc(ctx->virtual_desktop_ids,
                sizeof(*grown_virtual_desktops) * (size_t)next_capacity);
    if (grown_virtual_desktops == NULL) {
      ctx->status = WWMK_STATUS_OUT_OF_MEMORY;
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
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  *out = NULL;

  if (initialCap == 0) {
    initialCap = 16;
  }

  result.buffer = malloc(sizeof(*result.buffer) * (size_t)initialCap);
  if (result.buffer == NULL) {
    return WWMK_STATUS_OUT_OF_MEMORY;
  }
  result.virtual_desktop_ids =
      malloc(sizeof(*result.virtual_desktop_ids) * (size_t)initialCap);
  if (result.virtual_desktop_ids == NULL) {
    free(result.buffer);
    return WWMK_STATUS_OUT_OF_MEMORY;
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
    return WWMK_STATUS_ENUM_FAILED;
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
    return WWMK_STATUS_OUT_OF_MEMORY;
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
  if (window.hwnd == NULL) {
    return -1;
  }

  if (!MoveWindow((HWND)window.hwnd, rect.x, rect.y, rect.width, rect.height,
                  TRUE)) {
    return -2;
  }

  return 0;
}

WWMK_Monitor wwmk_monitor_from_window(WWMK_Window window) {
  HMONITOR monitor = NULL;
  WWMK_Monitor value = {0};

  if (window.hwnd == NULL) {
    return value;
  }

  monitor = MonitorFromWindow((HWND)window.hwnd, MONITOR_DEFAULTTONEAREST);
  if (monitor == NULL) {
    return value;
  }

  if (wwmk_fill_monitor_from_handle(monitor, &value) < 0) {
    return (WWMK_Monitor){0};
  }

  return value;
}

int wwmk_window_primary_monitor(WWMK_Window window, WWMK_Monitor *out) {
  WWMK_Monitor *monitors = NULL;
  WWMK_Rect window_rect = window.rect;
  WWMK_Monitor fallback_monitor = {0};
  int count = 0;
  int status = 0;
  int i = 0;
  int best_index = -1;
  long long best_area = -1;

  if (out == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  *out = (WWMK_Monitor){0};

  if (window.hwnd != NULL) {
    (void)wwmk_get_window_rect(window, &window_rect);
  }

  status = wwmk_collect_monitors(&monitors, &count);
  if (status < 0) {
    return status;
  }

  for (i = 0; i < count; i++) {
    WWMK_Rect intersection = {0};
    long long area = 0;

    if (wwmk_rect_intersection(window_rect, monitors[i].rect, &intersection) <
        0) {
      free(monitors);
      return WWMK_STATUS_ENUM_FAILED;
    }

    area = wwmk_rect_area_ll(intersection);
    if (area > best_area) {
      best_area = area;
      best_index = i;
    }
  }

  if (best_index >= 0 && best_area > 0) {
    *out = monitors[best_index];
    free(monitors);
    return 0;
  }

  if (window.hwnd != NULL) {
    fallback_monitor = wwmk_monitor_from_window(window);
    if (fallback_monitor.id != 0) {
      *out = fallback_monitor;
      free(monitors);
      return 0;
    }
  }

  free(monitors);
  return WWMK_STATUS_NOT_FOUND;
}

int wwmk_window_monitor_by_center(WWMK_Window window, WWMK_Monitor *out) {
  WWMK_Rect window_rect = window.rect;
  WWMK_Point center = {0};
  POINT win32_center = {0};
  HMONITOR monitor = NULL;

  if (out == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  *out = (WWMK_Monitor){0};

  if (window.hwnd != NULL) {
    (void)wwmk_get_window_rect(window, &window_rect);
  }

  center = wwmk_rect_center(window_rect);
  win32_center.x = center.x;
  win32_center.y = center.y;

  monitor = MonitorFromPoint(win32_center, MONITOR_DEFAULTTONULL);
  if (monitor == NULL) {
    return WWMK_STATUS_NOT_FOUND;
  }

  return wwmk_fill_monitor_from_handle(monitor, out);
}

int wwmk_rect_visible_region_on_monitors(WWMK_Rect rect, WWMK_Rect *out,
                                         int cap) {
  WWMK_Monitor *monitors = NULL;
  int count = 0;
  int status = 0;
  int i = 0;
  int visible_count = 0;

  if (cap < 0 || (cap > 0 && out == NULL)) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  if (rect.width <= 0 || rect.height <= 0) {
    return 0;
  }

  status = wwmk_collect_monitors(&monitors, &count);
  if (status < 0) {
    return status;
  }

  for (i = 0; i < count; i++) {
    WWMK_Rect intersection = {0};

    if (wwmk_rect_intersection(rect, monitors[i].rect, &intersection) < 0) {
      free(monitors);
      return WWMK_STATUS_ENUM_FAILED;
    }

    if (intersection.width > 0 && intersection.height > 0) {
      if (visible_count < cap) {
        out[visible_count] = intersection;
      }
      visible_count++;
    }
  }

  free(monitors);
  return visible_count;
}

int wwmk_rect_is_fully_offscreen(WWMK_Rect rect) {
  int visible_regions = wwmk_rect_visible_region_on_monitors(rect, NULL, 0);

  if (visible_regions < 0) {
    return visible_regions;
  }

  return visible_regions == 0 ? 1 : 0;
}

int wwmk_rect_is_partially_visible(WWMK_Rect rect) {
  WWMK_Monitor *monitors = NULL;
  int count = 0;
  int status = 0;
  int i = 0;
  long long visible_area = 0;
  long long total_area = wwmk_rect_area_ll(rect);

  if (total_area == 0) {
    return 0;
  }

  status = wwmk_collect_monitors(&monitors, &count);
  if (status < 0) {
    return status;
  }

  for (i = 0; i < count; i++) {
    WWMK_Rect intersection = {0};

    if (wwmk_rect_intersection(rect, monitors[i].rect, &intersection) < 0) {
      free(monitors);
      return WWMK_STATUS_ENUM_FAILED;
    }

    visible_area += wwmk_rect_area_ll(intersection);
  }

  free(monitors);

  if (visible_area == 0) {
    return 0;
  }

  return visible_area < total_area ? 1 : 0;
}

int wwmk_get_monitor_layout_bounds(WWMK_Rect *out) {
  WWMK_Monitor *monitors = NULL;
  int count = 0;
  int status = 0;
  int i = 0;
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  if (out == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  *out = (WWMK_Rect){0};

  status = wwmk_collect_monitors(&monitors, &count);
  if (status < 0) {
    return status;
  }

  if (count == 0) {
    free(monitors);
    return WWMK_STATUS_NOT_FOUND;
  }

  left = monitors[0].rect.x;
  top = monitors[0].rect.y;
  right = monitors[0].rect.x + monitors[0].rect.width;
  bottom = monitors[0].rect.y + monitors[0].rect.height;

  for (i = 1; i < count; i++) {
    int monitor_right = monitors[i].rect.x + monitors[i].rect.width;
    int monitor_bottom = monitors[i].rect.y + monitors[i].rect.height;

    if (monitors[i].rect.x < left) {
      left = monitors[i].rect.x;
    }
    if (monitors[i].rect.y < top) {
      top = monitors[i].rect.y;
    }
    if (monitor_right > right) {
      right = monitor_right;
    }
    if (monitor_bottom > bottom) {
      bottom = monitor_bottom;
    }
  }

  out->x = left;
  out->y = top;
  out->width = right - left;
  out->height = bottom - top;

  free(monitors);
  return 0;
}

int wwmk_get_uncovered_regions(WWMK_Rect *out, int cap) {
  WWMK_Monitor *monitors = NULL;
  WWMK_Rect layout = {0};
  WWMK_Rect *current = NULL;
  int current_count = 0;
  int count = 0;
  int status = 0;
  int i = 0;
  int j = 0;
  int copy_count = 0;

  if (cap < 0 || (cap > 0 && out == NULL)) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  status = wwmk_collect_monitors(&monitors, &count);
  if (status < 0) {
    return status;
  }

  if (count == 0) {
    free(monitors);
    return 0;
  }

  status = wwmk_get_monitor_layout_bounds(&layout);
  if (status < 0) {
    free(monitors);
    return status;
  }

  current = malloc(sizeof(*current));
  if (current == NULL) {
    free(monitors);
    return WWMK_STATUS_OUT_OF_MEMORY;
  }
  current[0] = layout;
  current_count = 1;

  for (i = 0; i < count; i++) {
    WWMK_Rect *next = NULL;
    int next_count = 0;
    int next_cap = 0;

    for (j = 0; j < current_count; j++) {
      WWMK_Rect intersection = {0};
      WWMK_Rect pieces[4] = {0};
      int piece_count = 0;
      int source_right = current[j].x + current[j].width;
      int source_bottom = current[j].y + current[j].height;
      int intersection_right = 0;
      int intersection_bottom = 0;
      int k = 0;

      status = wwmk_rect_intersection(current[j], monitors[i].rect, &intersection);
      if (status < 0) {
        free(current);
        free(next);
        free(monitors);
        return status;
      }

      if (status == 0) {
        pieces[0] = current[j];
        piece_count = 1;
      } else {
        intersection_right = intersection.x + intersection.width;
        intersection_bottom = intersection.y + intersection.height;

        if (intersection.y > current[j].y) {
          pieces[piece_count++] =
              (WWMK_Rect){current[j].x, current[j].y, current[j].width,
                          intersection.y - current[j].y};
        }

        if (intersection_bottom < source_bottom) {
          pieces[piece_count++] =
              (WWMK_Rect){current[j].x, intersection_bottom, current[j].width,
                          source_bottom - intersection_bottom};
        }

        if (intersection.x > current[j].x) {
          pieces[piece_count++] =
              (WWMK_Rect){current[j].x, intersection.y,
                          intersection.x - current[j].x, intersection.height};
        }

        if (intersection_right < source_right) {
          pieces[piece_count++] =
              (WWMK_Rect){intersection_right, intersection.y,
                          source_right - intersection_right,
                          intersection.height};
        }
      }

      for (k = 0; k < piece_count; k++) {
        WWMK_Rect *grown_next = NULL;

        if (pieces[k].width <= 0 || pieces[k].height <= 0) {
          continue;
        }

        if (next_count >= next_cap) {
          int next_capacity = next_cap > 0 ? next_cap * 2 : 4;

          grown_next =
              realloc(next, sizeof(*grown_next) * (size_t)next_capacity);
          if (grown_next == NULL) {
            free(current);
            free(next);
            free(monitors);
            return WWMK_STATUS_OUT_OF_MEMORY;
          }

          next = grown_next;
          next_cap = next_capacity;
        }

        next[next_count] = pieces[k];
        next_count++;
      }
    }

    free(current);
    current = next;
    current_count = next_count;
  }

  copy_count = current_count < cap ? current_count : cap;
  for (i = 0; i < copy_count; i++) {
    out[i] = current[i];
  }

  count = current_count;
  free(current);
  free(monitors);
  return count;
}

int wwmk_rect_is_visible_on_any_monitor(WWMK_Rect rect) {
  int visible_regions = wwmk_rect_visible_region_on_monitors(rect, NULL, 0);

  if (visible_regions < 0) {
    return visible_regions;
  }

  return visible_regions > 0 ? 1 : 0;
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
