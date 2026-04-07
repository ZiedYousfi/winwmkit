#include "winwmkit/winwmkit.h"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

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

static WWMK_Point wwmk_todo_point(void) {
  WWMK_Point value = {0};
  wwmk_todo_abort();
  return value;
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
  (void)out;
  (void)cap;
  return wwmk_todo_int();
}

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
  (void)lParam;
  char title[256];

  GetWindowTextA(hwnd, title, sizeof(title));

  printf("HWND: %p | Titre: %s | ", (void *)hwnd, title);

  if (IsIconic(hwnd)) {
    printf("minimisée");
  } else if (IsWindowVisible(hwnd)) {
    printf("visible");
  } else {
    printf("cachée/non visible");
  }

  printf("\n");
  return TRUE;
}

int wwmk_get_windows(WWMK_Window *out, int cap) {
  (void)out;
  (void)cap;
  EnumWindows(EnumWindowsCallback, 0);
  return wwmk_todo_int();
}

int wwmk_get_window_rect(WWMK_Window window, WWMK_Rect *out) {
  (void)window;
  (void)out;
  return wwmk_todo_int();
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
  (void)window;
  (void)monitor;
  return wwmk_todo_int();
}

WWMK_Monitor wwmk_monitor_from_window(WWMK_Window window) {
  (void)window;
  return wwmk_todo_monitor();
}

int wwmk_rect_intersects(WWMK_Rect a, WWMK_Rect b) {
  (void)a;
  (void)b;
  return wwmk_todo_int();
}

int wwmk_rect_contains_point(WWMK_Rect rect, WWMK_Point point) {
  (void)rect;
  (void)point;
  return wwmk_todo_int();
}

int wwmk_rect_intersection(WWMK_Rect a, WWMK_Rect b, WWMK_Rect *out) {
  (void)a;
  (void)b;
  (void)out;
  return wwmk_todo_int();
}

int wwmk_rect_intersection_area(WWMK_Rect a, WWMK_Rect b, int *out) {
  (void)a;
  (void)b;
  (void)out;
  return wwmk_todo_int();
}

int wwmk_window_intersects_monitor(WWMK_Window window, WWMK_Monitor monitor) {
  (void)window;
  (void)monitor;
  return wwmk_todo_int();
}

int wwmk_window_intersection_area_with_monitor(WWMK_Window window,
                                               WWMK_Monitor monitor, int *out) {
  (void)window;
  (void)monitor;
  (void)out;
  return wwmk_todo_int();
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
  (void)out;
  return wwmk_todo_int();
}

WWMK_Point wwmk_rect_center(WWMK_Rect rect) {
  (void)rect;
  return wwmk_todo_point();
}
