#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "winwmkit/winwmkit.h"

static const char *window_title(const WWMK_Window *window) {
  if (window == NULL || window->title[0] == '\0') {
    return "<untitled>";
  }

  return window->title;
}

static void print_rect(const char *label, WWMK_Rect rect) {
  printf("%s{x=%d, y=%d, width=%d, height=%d}\n", label, rect.x, rect.y,
         rect.width, rect.height);
}

static void print_point(const char *label, WWMK_Point point) {
  printf("%s{x=%d, y=%d}\n", label, point.x, point.y);
}

static void print_monitor(const char *label, const WWMK_Monitor *monitor) {
  printf("%s id=%p is_primary=%d\n", label, (void *)monitor->id,
         monitor->is_primary);
  print_rect("  rect=", monitor->rect);
  print_rect("  work_rect=", monitor->work_rect);
}

static void print_window(const char *label, const WWMK_Window *window) {
  printf(
      "%s hwnd=%p title=\"%s\" visible=%d minimized=%d maximized=%d "
      "has_virtual_desktop=%d virtual_desktop=%p\n",
      label, window->hwnd, window_title(window), window->is_visible,
      window->is_minimized, window->is_maximized, window->has_virtual_desktop,
      window->virtual_desktop);
  print_rect("  rect=", window->rect);
}

static int load_monitors(WWMK_Monitor **out) {
  int count = 0;
  int fetched = 0;

  *out = NULL;

  count = wwmk_get_monitors(NULL, 0);
  printf("wwmk_get_monitors(NULL, 0) => %d\n", count);
  if (count <= 0) {
    return count;
  }

  *out = (WWMK_Monitor *)calloc((size_t)count, sizeof(**out));
  if (*out == NULL) {
    fprintf(stderr, "Failed to allocate monitor buffer for %d entries.\n",
            count);
    return -1000;
  }

  fetched = wwmk_get_monitors(*out, count);
  printf("wwmk_get_monitors(buffer, %d) => %d\n", count, fetched);
  if (fetched < 0) {
    free(*out);
    *out = NULL;
    return fetched;
  }

  return fetched;
}

static int load_uncovered_regions(WWMK_Rect **out) {
  int count = 0;
  int fetched = 0;

  *out = NULL;

  count = wwmk_get_uncovered_regions(NULL, 0);
  printf("wwmk_get_uncovered_regions(NULL, 0) => %d\n", count);
  if (count <= 0) {
    return count;
  }

  *out = (WWMK_Rect *)calloc((size_t)count, sizeof(**out));
  if (*out == NULL) {
    fprintf(stderr,
            "Failed to allocate uncovered-region buffer for %d entries.\n",
            count);
    return -1000;
  }

  fetched = wwmk_get_uncovered_regions(*out, count);
  printf("wwmk_get_uncovered_regions(buffer, %d) => %d\n", count, fetched);
  if (fetched < 0) {
    free(*out);
    *out = NULL;
    return fetched;
  }

  return fetched;
}

static int load_visible_regions_for_rect(WWMK_Rect rect, WWMK_Rect **out) {
  int count = 0;
  int fetched = 0;

  *out = NULL;

  count = wwmk_rect_visible_region_on_monitors(rect, NULL, 0);
  printf("  wwmk_rect_visible_region_on_monitors(rect, NULL, 0) => %d\n",
         count);
  if (count <= 0) {
    return count;
  }

  *out = (WWMK_Rect *)calloc((size_t)count, sizeof(**out));
  if (*out == NULL) {
    fprintf(stderr, "Failed to allocate visible-region buffer for %d entries.\n",
            count);
    return -1000;
  }

  fetched = wwmk_rect_visible_region_on_monitors(rect, *out, count);
  printf("  wwmk_rect_visible_region_on_monitors(rect, buffer, %d) => %d\n",
         count, fetched);
  if (fetched < 0) {
    free(*out);
    *out = NULL;
    return fetched;
  }

  return fetched;
}

static const char *shuffle_skip_reason(const WWMK_Window *window) {
  char class_name[256] = {0};
  HWND hwnd = NULL;

  if (window == NULL || window->hwnd == NULL) {
    return "null hwnd";
  }

  if (!window->is_visible) {
    return "hidden";
  }

  if (window->is_minimized) {
    return "minimized";
  }

  if (window->is_maximized) {
    return "maximized";
  }

  if (window->rect.width <= 0 || window->rect.height <= 0) {
    return "empty rect";
  }

  hwnd = (HWND)window->hwnd;
  if (hwnd == GetShellWindow()) {
    return "shell window";
  }

  GetClassNameA(hwnd, class_name, (int)sizeof(class_name));
  if (strcmp(class_name, "Shell_TrayWnd") == 0 ||
      strcmp(class_name, "Progman") == 0 ||
      strcmp(class_name, "WorkerW") == 0) {
    return "desktop shell surface";
  }

  return NULL;
}

static WWMK_Rect fit_rect_inside(WWMK_Rect source, WWMK_Rect bounds) {
  const int margin = 24;
  WWMK_Rect result = source;
  int available_width = bounds.width - margin * 2;
  int available_height = bounds.height - margin * 2;

  if (available_width <= 0 || available_height <= 0) {
    return bounds;
  }

  if (result.width > available_width) {
    result.width = available_width;
  }
  if (result.height > available_height) {
    result.height = available_height;
  }

  if (result.width <= 0) {
    result.width = available_width;
  }
  if (result.height <= 0) {
    result.height = available_height;
  }

  result.x = bounds.x + margin + (available_width - result.width) / 2;
  result.y = bounds.y + margin + (available_height - result.height) / 2;
  return result;
}

static void log_global_geometry(const WWMK_Monitor *monitors, int monitor_count) {
  WWMK_Rect layout = {0};
  WWMK_Rect virtual_space = {0};
  WWMK_Rect *uncovered_regions = NULL;
  int uncovered_count = 0;
  int status = 0;
  int i = 0;

  status = wwmk_get_monitor_layout_bounds(&layout);
  printf("wwmk_get_monitor_layout_bounds(&layout) => %d\n", status);
  if (status == 0) {
    print_rect("monitor layout bounds=", layout);
  }

  status = wwmk_get_virtual_space(&virtual_space);
  printf("wwmk_get_virtual_space(&virtual_space) => %d\n", status);
  if (status == 0) {
    print_rect("virtual space=", virtual_space);
  }

  uncovered_count = load_uncovered_regions(&uncovered_regions);
  if (uncovered_count > 0) {
    for (i = 0; i < uncovered_count; i++) {
      char label[64] = {0};
      snprintf(label, sizeof(label), "uncovered_regions[%d]=", i);
      print_rect(label, uncovered_regions[i]);
    }
  }

  free(uncovered_regions);

  for (i = 0; i < monitor_count; i++) {
    char label[64] = {0};
    snprintf(label, sizeof(label), "monitor[%d]", i);
    print_monitor(label, &monitors[i]);
  }
}

static void exercise_window_geometry(WWMK_Window *window,
                                     const WWMK_Monitor *monitors,
                                     int monitor_count) {
  WWMK_Rect live_rect = {0};
  WWMK_Rect *visible_regions = NULL;
  WWMK_Monitor nearest = {0};
  WWMK_Monitor primary = {0};
  WWMK_Monitor center_monitor = {0};
  WWMK_Point center = {0};
  int visible_region_count = 0;
  int fully_offscreen = 0;
  int partially_visible = 0;
  int visible_any = 0;
  int status = 0;
  int i = 0;

  print_window("window", window);

  status = wwmk_get_window_rect(*window, &live_rect);
  printf("  wwmk_get_window_rect(window, &live_rect) => %d\n", status);
  if (status == 0) {
    window->rect = live_rect;
    print_rect("  live_rect=", live_rect);
  }

  center = wwmk_rect_center(window->rect);
  print_point("  wwmk_rect_center(window.rect) => ", center);
  printf("  wwmk_rect_contains_point(window.rect, center) => %d\n",
         wwmk_rect_contains_point(window->rect, center));

  nearest = wwmk_monitor_from_window(*window);
  printf("  wwmk_monitor_from_window(window) => id=%p\n", (void *)nearest.id);
  if (nearest.id != 0) {
    print_monitor("  nearest_monitor", &nearest);
  }

  status = wwmk_window_primary_monitor(*window, &primary);
  printf("  wwmk_window_primary_monitor(window, &primary) => %d\n", status);
  if (status == 0) {
    print_monitor("  primary_monitor", &primary);
  }

  status = wwmk_window_monitor_by_center(*window, &center_monitor);
  printf("  wwmk_window_monitor_by_center(window, &center_monitor) => %d\n",
         status);
  if (status == 0) {
    print_monitor("  center_monitor", &center_monitor);
  }

  visible_region_count = load_visible_regions_for_rect(window->rect,
                                                       &visible_regions);
  if (visible_region_count > 0) {
    for (i = 0; i < visible_region_count; i++) {
      char label[64] = {0};
      snprintf(label, sizeof(label), "  visible_region[%d]=", i);
      print_rect(label, visible_regions[i]);
    }
  }
  free(visible_regions);

  fully_offscreen = wwmk_rect_is_fully_offscreen(window->rect);
  partially_visible = wwmk_rect_is_partially_visible(window->rect);
  visible_any = wwmk_rect_is_visible_on_any_monitor(window->rect);
  printf("  wwmk_rect_is_fully_offscreen(window.rect) => %d\n",
         fully_offscreen);
  printf("  wwmk_rect_is_partially_visible(window.rect) => %d\n",
         partially_visible);
  printf("  wwmk_rect_is_visible_on_any_monitor(window.rect) => %d\n",
         visible_any);

  for (i = 0; i < monitor_count; i++) {
    WWMK_Rect intersection = {0};
    int intersection_area = 0;
    int rect_intersection_area = 0;
    int window_is_on_monitor = wwmk_window_is_on_monitor(*window, monitors[i]);
    int window_intersects_monitor =
        wwmk_window_intersects_monitor(*window, monitors[i]);
    int rect_intersects =
        wwmk_rect_intersects(window->rect, monitors[i].rect);
    int rect_intersection =
        wwmk_rect_intersection(window->rect, monitors[i].rect, &intersection);
    int window_area_status =
        wwmk_window_intersection_area_with_monitor(*window, monitors[i],
                                                  &intersection_area);
    int rect_area_status = wwmk_rect_intersection_area(
        window->rect, monitors[i].rect, &rect_intersection_area);

    printf("  monitor[%d] comparisons\n", i);
    printf("    wwmk_window_is_on_monitor(window, monitor[%d]) => %d\n", i,
           window_is_on_monitor);
    printf("    wwmk_window_intersects_monitor(window, monitor[%d]) => %d\n", i,
           window_intersects_monitor);
    printf("    wwmk_rect_intersects(window.rect, monitor[%d].rect) => %d\n", i,
           rect_intersects);
    printf(
        "    wwmk_rect_intersection(window.rect, monitor[%d].rect, "
        "&intersection) => %d\n",
        i, rect_intersection);
    print_rect("    intersection=", intersection);
    printf(
        "    wwmk_window_intersection_area_with_monitor(window, monitor[%d], "
        "&area) => %d (area=%d)\n",
        i, window_area_status, intersection_area);
    printf(
        "    wwmk_rect_intersection_area(window.rect, monitor[%d].rect, "
        "&area) => %d (area=%d)\n",
        i, rect_area_status, rect_intersection_area);
  }
}

static void shuffle_windows(WWMK_Window *windows, int window_count,
                            int monitor_count) {
  int *eligible_indices = NULL;
  WWMK_Rect *saved_rects = NULL;
  int eligible_count = 0;
  int i = 0;

  if (window_count <= 0) {
    printf("No windows available for shuffle.\n");
    return;
  }

  eligible_indices = (int *)calloc((size_t)window_count, sizeof(*eligible_indices));
  saved_rects = (WWMK_Rect *)calloc((size_t)window_count, sizeof(*saved_rects));
  if (eligible_indices == NULL || saved_rects == NULL) {
    fprintf(stderr, "Failed to allocate shuffle bookkeeping.\n");
    free(eligible_indices);
    free(saved_rects);
    return;
  }

  for (i = 0; i < window_count; i++) {
    const char *reason = shuffle_skip_reason(&windows[i]);

    if (reason != NULL) {
      printf("Skipping shuffle for \"%s\" (%p): %s\n", window_title(&windows[i]),
             windows[i].hwnd, reason);
      continue;
    }

    eligible_indices[eligible_count] = i;
    saved_rects[eligible_count] = windows[i].rect;
    eligible_count++;
  }

  printf("Shuffle candidates => %d of %d windows.\n", eligible_count,
         window_count);

  if (eligible_count == 0) {
    free(eligible_indices);
    free(saved_rects);
    return;
  }

  if (eligible_count == 1) {
    int window_index = eligible_indices[0];
    WWMK_Rect target = windows[window_index].rect;
    WWMK_Monitor destination = {0};
    int status = 0;

    if (monitor_count > 0) {
      status = wwmk_window_primary_monitor(windows[window_index], &destination);
      printf(
          "Single-window shuffle: wwmk_window_primary_monitor(window, "
          "&destination) => %d\n",
          status);
      if (status == 0) {
        target = fit_rect_inside(windows[window_index].rect, destination.work_rect);
      }
    }

    printf("Single-window shuffle target for \"%s\"\n",
           window_title(&windows[window_index]));
    print_rect("  target=", target);
    status = wwmk_set_window_rect(windows[window_index], target);
    printf("  wwmk_set_window_rect(window, target) => %d\n", status);
    if (status == 0) {
      (void)wwmk_get_window_rect(windows[window_index], &windows[window_index].rect);
      print_rect("  post_shuffle_rect=", windows[window_index].rect);
    }

    free(eligible_indices);
    free(saved_rects);
    return;
  }

  for (i = 0; i < eligible_count; i++) {
    int source_index = eligible_indices[i];
    int destination_slot = (i + 1) % eligible_count;
    WWMK_Rect target = saved_rects[destination_slot];
    int status = 0;

    printf("Shuffle move %d/%d for \"%s\"\n", i + 1, eligible_count,
           window_title(&windows[source_index]));
    print_rect("  source_saved_rect=", saved_rects[i]);
    print_rect("  target_rect=", target);

    status = wwmk_set_window_rect(windows[source_index], target);
    printf("  wwmk_set_window_rect(window, target_rect) => %d\n", status);
    if (status == 0) {
      status = wwmk_get_window_rect(windows[source_index],
                                    &windows[source_index].rect);
      printf("  wwmk_get_window_rect(window, &window.rect) => %d\n", status);
      if (status == 0) {
        print_rect("  post_shuffle_rect=", windows[source_index].rect);
      }
    }
  }

  free(eligible_indices);
  free(saved_rects);
}

int main(void) {
  WWMK_Monitor *monitors = NULL;
  WWMK_Window *windows = NULL;
  int monitor_count = 0;
  int window_count = 0;
  int i = 0;

  printf("WinWMKit example: enumerate monitors, inspect windows, and shuffle "
         "eligible windows.\n");
  printf("Skipping event API calls because the current implementation aborts.\n");

  monitor_count = load_monitors(&monitors);
  if (monitor_count < 0) {
    fprintf(stderr, "Failed to load monitors. error=%d\n", monitor_count);
    free(monitors);
    return 1;
  }

  log_global_geometry(monitors, monitor_count);

  window_count = wwmk_get_windows(&windows, 0);
  printf("wwmk_get_windows(&windows, 0) => %d\n", window_count);
  if (window_count < 0) {
    fprintf(stderr, "Failed to get windows. error=%d\n", window_count);
    free(monitors);
    free(windows);
    return 1;
  }

  for (i = 0; i < window_count; i++) {
    printf("\n=== Window %d/%d ===\n", i + 1, window_count);
    exercise_window_geometry(&windows[i], monitors, monitor_count);
  }

  printf("\n=== Shuffle Pass ===\n");
  shuffle_windows(windows, window_count, monitor_count);

  free(monitors);
  free(windows);
  return 0;
}
