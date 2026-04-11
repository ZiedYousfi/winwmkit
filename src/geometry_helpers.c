#include "winwmkit/winwmkit.h"

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

int wwmk_window_is_on_monitor(WWMK_Window window, WWMK_Monitor monitor) {
  return wwmk_rect_intersects(window.rect, monitor.rect);
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

WWMK_Point wwmk_rect_center(WWMK_Rect rect) {
  WWMK_Point value = {0};

  value.x = rect.x + rect.width / 2;
  value.y = rect.y + rect.height / 2;
  return value;
}
