#pragma once

#ifdef WWMK_BUILD_DLL
#define WWMK_API __declspec(dllexport)
#elif defined(WWMK_USE_DLL)
#define WWMK_API __declspec(dllimport)
#else
#define WWMK_API
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @file winwmkit.h
 *  @brief Public API for window and monitor enumeration on Windows.
 */

/** @brief A 2D point in screen coordinates. */
typedef struct {
  /** Horizontal position. */
  int x;
  /** Vertical position. */
  int y;
} WWMK_Point;

/** @brief A 2D size in pixels. */
typedef struct {
  /** Width in pixels. */
  int width;
  /** Height in pixels. */
  int height;
} WWMK_Size;

/** @brief An axis-aligned rectangle in screen coordinates. */
typedef struct {
  /** Left edge. */
  int x;
  /** Top edge. */
  int y;
  /** Rectangle width in pixels. */
  int width;
  /** Rectangle height in pixels. */
  int height;
} WWMK_Rect;

/** @brief Description of a display monitor. */
typedef struct {
  /** Opaque monitor identifier derived from the native monitor handle. */
  uintptr_t id;
  /** Full monitor bounds. */
  WWMK_Rect rect;
  /** Work area excluding taskbars and reserved desktop UI. */
  WWMK_Rect work_rect;
  /** Non-zero when this is the primary monitor. */
  int is_primary;
} WWMK_Monitor;

/** @brief Snapshot of a top-level window. */
typedef struct {
  /** Native `HWND` stored as an opaque pointer. */
  void *hwnd;
  /** Null-terminated window title buffer. */
  char title[256];
  /** Window bounds in screen coordinates. */
  WWMK_Rect rect;
  /** Non-zero when the window is visible. */
  int is_visible;
  /** Non-zero when the window is minimized. */
  int is_minimized;
  /** Non-zero when the window is maximized. */
  int is_maximized;
  /** Points to GUID storage owned by the buffer returned by `wwmk_get_windows`.
   */
  void *virtual_desktop;
  /** Non-zero when `virtual_desktop` contains a valid desktop identifier. */
  int has_virtual_desktop;
} WWMK_Window;

/** @brief Event kinds emitted by the event API. */
typedef enum {
  /** No event. */
  WWMK_EVENT_NONE = 0,
  /** A window was created. */
  WWMK_EVENT_WINDOW_CREATED,
  /** A window was destroyed. */
  WWMK_EVENT_WINDOW_DESTROYED,
  /** A window changed position. */
  WWMK_EVENT_WINDOW_MOVED,
  /** A window changed size. */
  WWMK_EVENT_WINDOW_RESIZED,
  /** A window gained focus. */
  WWMK_EVENT_WINDOW_FOCUSED,
  /** Monitor configuration changed. */
  WWMK_EVENT_MONITOR_CHANGED
} WWMK_EventType;

/** @brief Event payload delivered to callbacks. */
typedef struct {
  /** Event kind. */
  WWMK_EventType type;
  /** Window identifier when the event targets a window. */
  uintptr_t window_id;
  /** Monitor identifier when the event targets a monitor. */
  uintptr_t monitor_id;
} WWMK_Event;

/** @brief Callback signature for event notifications. */
typedef void (*WWMK_EventCallback)(const WWMK_Event *event, void *userdata);

/** @brief Opaque named-pipe server handle. */
typedef struct WWMK_PipeServer WWMK_PipeServer;

/** @brief Callback signature for incoming pipe messages. */
typedef void (*WWMK_PipeMessageCallback)(const char *message, size_t size,
                                         void *userdata);

/**
 * @brief Registers a generic event callback.
 * @param callback Callback invoked for emitted events.
 * @param userdata Opaque user pointer passed back to @p callback.
 * @return Status code from the event backend.
 * @note Event backend support is not implemented yet.
 */
WWMK_API int wwmk_set_event_callback(WWMK_EventCallback callback,
                                     void *userdata);

/**
 * @brief Starts the event backend.
 * @return Status code from the event backend.
 * @note Event backend support is not implemented yet.
 */
WWMK_API int wwmk_start(void);

/**
 * @brief Stops the event backend.
 * @return Status code from the event backend.
 * @note Event backend support is not implemented yet.
 */
WWMK_API int wwmk_stop(void);

/**
 * @brief Registers a callback for window creation events.
 * @param callback Callback invoked when a window is created.
 * @param userdata Opaque user pointer passed back to @p callback.
 * @return Status code from the event backend.
 * @note Event backend support is not implemented yet.
 */
WWMK_API int wwmk_on_window_created(WWMK_EventCallback callback,
                                    void *userdata);

/**
 * @brief Registers a callback for window destruction events.
 * @param callback Callback invoked when a window is destroyed.
 * @param userdata Opaque user pointer passed back to @p callback.
 * @return Status code from the event backend.
 * @note Event backend support is not implemented yet.
 */
WWMK_API int wwmk_on_window_destroyed(WWMK_EventCallback callback,
                                      void *userdata);

/**
 * @brief Registers a callback for window move events.
 * @param callback Callback invoked when a window moves.
 * @param userdata Opaque user pointer passed back to @p callback.
 * @return Status code from the event backend.
 * @note Event backend support is not implemented yet.
 */
WWMK_API int wwmk_on_window_moved(WWMK_EventCallback callback, void *userdata);

/**
 * @brief Registers a callback for monitor layout change events.
 * @param callback Callback invoked when monitor state changes.
 * @param userdata Opaque user pointer passed back to @p callback.
 * @return Status code from the event backend.
 * @note Event backend support is not implemented yet.
 */
WWMK_API int wwmk_on_monitor_changed(WWMK_EventCallback callback,
                                     void *userdata);

/**
 * @brief Starts a background named-pipe server.
 * @param pipe_name Pipe name such as `my-pipe` or `\\.\pipe\my-pipe`.
 * @param callback Callback invoked for each complete message received.
 * @param userdata Opaque user pointer passed back to @p callback.
 * @return Server handle on success, or `NULL` on failure.
 * @note Release the returned handle with `wwmk_pipe_server_stop`.
 */
WWMK_API WWMK_PipeServer *
wwmk_pipe_server_start(const char *pipe_name,
                       WWMK_PipeMessageCallback callback, void *userdata);

/**
 * @brief Stops a named-pipe server created by `wwmk_pipe_server_start`.
 * @param server Server handle to stop.
 * @return `0` on success, or a negative error code.
 */
WWMK_API int wwmk_pipe_server_stop(WWMK_PipeServer *server);

/**
 * @brief Enumerates connected monitors.
 * @param out Destination buffer for copied monitor entries.
 * @param cap Number of monitor entries that fit in @p out.
 * @return Total number of detected monitors, or a negative error code.
 * @note Pass `NULL` with `cap == 0` to query the required count only.
 */
WWMK_API int wwmk_get_monitors(WWMK_Monitor *out, int cap);

/**
 * @brief Enumerates top-level windows.
 * @param[out] out Receives a heap allocation containing the window list.
 * @param cap Initial allocation hint used while enumerating windows.
 * @return Number of windows written to `*out`, `0` when none are found, or a
 * negative error code.
 * @note Release `*out` with `free()`.
 * @note When present, `virtual_desktop` points into the same allocation as the
 * returned window array and becomes invalid after `free(*out)`.
 */
WWMK_API int wwmk_get_windows(WWMK_Window **out, int cap);

/**
 * @brief Reads the current bounds of a window.
 * @param window Window snapshot or handle wrapper.
 * @param[out] out Receives the current rectangle.
 * @return `0` on success, or a negative error code.
 */
WWMK_API int wwmk_get_window_rect(WWMK_Window window, WWMK_Rect *out);

/**
 * @brief Moves and resizes a window.
 * @param window Window snapshot or handle wrapper.
 * @param rect Target rectangle.
 * @return `0` on success, or a negative error code.
 */
WWMK_API int wwmk_set_window_rect(WWMK_Window window, WWMK_Rect rect);

/**
 * @brief Tests whether a window rectangle intersects a monitor rectangle.
 * @param window Window to test.
 * @param monitor Monitor to test against.
 * @return `1` when the rectangles intersect, `0` when they do not, or a
 * negative error code.
 */
WWMK_API int wwmk_window_is_on_monitor(WWMK_Window window,
                                       WWMK_Monitor monitor);

/**
 * @brief Returns the nearest monitor for a window.
 * @param window Window to inspect.
 * @return Monitor description, or a zero-initialized monitor when no monitor
 * can be resolved.
 */
WWMK_API WWMK_Monitor wwmk_monitor_from_window(WWMK_Window window);

/**
 * @brief Tests whether two rectangles intersect.
 * @param a First rectangle.
 * @param b Second rectangle.
 * @return `1` when the rectangles intersect, `0` when they do not.
 */
WWMK_API int wwmk_rect_intersects(WWMK_Rect a, WWMK_Rect b);

/**
 * @brief Tests whether a point lies inside a rectangle.
 * @param rect Rectangle to test.
 * @param point Point to test.
 * @return Non-zero when @p point is inside @p rect, otherwise `0`.
 */
WWMK_API int wwmk_rect_contains_point(WWMK_Rect rect, WWMK_Point point);

/**
 * @brief Computes the overlapping area of two rectangles.
 * @param a First rectangle.
 * @param b Second rectangle.
 * @param[out] out Receives the intersection rectangle.
 * @return `1` when an intersection exists, `0` when there is no overlap, or a
 * negative error code.
 */
WWMK_API int wwmk_rect_intersection(WWMK_Rect a, WWMK_Rect b, WWMK_Rect *out);

/**
 * @brief Computes the area of overlap between two rectangles.
 * @param a First rectangle.
 * @param b Second rectangle.
 * @param[out] out Receives the intersection area in square pixels.
 * @return `1` when an intersection exists, `0` when there is no overlap, or a
 * negative error code.
 */
WWMK_API int wwmk_rect_intersection_area(WWMK_Rect a, WWMK_Rect b, int *out);

/**
 * @brief Tests whether a window intersects a monitor.
 * @param window Window to test.
 * @param monitor Monitor to test against.
 * @return `1` when the rectangles intersect, `0` when they do not.
 */
WWMK_API int wwmk_window_intersects_monitor(WWMK_Window window,
                                            WWMK_Monitor monitor);

/**
 * @brief Computes a window's overlap area with a monitor.
 * @param window Window to test.
 * @param monitor Monitor to test against.
 * @param[out] out Receives the overlap area in square pixels.
 * @return `1` when an intersection exists, `0` when there is no overlap, or a
 * negative error code.
 */
WWMK_API int wwmk_window_intersection_area_with_monitor(WWMK_Window window,
                                                        WWMK_Monitor monitor,
                                                        int *out);

/**
 * @brief Finds the monitor containing the largest visible portion of a window.
 * @param window Window to inspect.
 * @param[out] out Receives the selected monitor.
 * @return `0` on success, or a negative error code.
 */
WWMK_API int wwmk_window_primary_monitor(WWMK_Window window, WWMK_Monitor *out);

/**
 * @brief Finds the monitor containing the center point of a window.
 * @param window Window to inspect.
 * @param[out] out Receives the selected monitor.
 * @return `0` on success, or a negative error code.
 */
WWMK_API int wwmk_window_monitor_by_center(WWMK_Window window,
                                           WWMK_Monitor *out);

/**
 * @brief Computes the visible fragments of a rectangle across all monitors.
 * @param rect Rectangle to clip against monitor bounds.
 * @param[out] out Destination buffer for visible fragments.
 * @param cap Number of entries available in @p out.
 * @return Total number of visible fragments, or a negative error code.
 * @note Pass `NULL` with `cap == 0` to query the required fragment count only.
 */
WWMK_API int wwmk_rect_visible_region_on_monitors(WWMK_Rect rect,
                                                  WWMK_Rect *out, int cap);

/**
 * @brief Tests whether a rectangle is completely outside all monitors.
 * @param rect Rectangle to test.
 * @return `1` when fully offscreen, `0` when any portion is visible, or a
 * negative error code.
 */
WWMK_API int wwmk_rect_is_fully_offscreen(WWMK_Rect rect);

/**
 * @brief Tests whether a rectangle is only partly visible on monitors.
 * @param rect Rectangle to test.
 * @return `1` when partially visible, `0` when fully visible or fully hidden,
 * or a negative error code.
 */
WWMK_API int wwmk_rect_is_partially_visible(WWMK_Rect rect);

/**
 * @brief Computes the bounding rectangle of the current monitor layout.
 * @param[out] out Receives the combined bounds.
 * @return `0` on success, or a negative error code.
 */
WWMK_API int wwmk_get_monitor_layout_bounds(WWMK_Rect *out);

/**
 * @brief Computes uncovered areas inside the overall monitor layout bounds.
 * @param[out] out Destination buffer for uncovered rectangles.
 * @param cap Number of entries available in @p out.
 * @return Total number of uncovered rectangles, or a negative error code.
 * @note Pass `NULL` with `cap == 0` to query the required rectangle count only.
 */
WWMK_API int wwmk_get_uncovered_regions(WWMK_Rect *out, int cap);

/**
 * @brief Tests whether any part of a rectangle is visible on a monitor.
 * @param rect Rectangle to test.
 * @return `1` when visible on at least one monitor, `0` otherwise, or a
 * negative error code.
 */
WWMK_API int wwmk_rect_is_visible_on_any_monitor(WWMK_Rect rect);

/**
 * @brief Returns the virtual desktop bounds reported by Windows.
 * @param[out] out Receives the virtual screen rectangle.
 * @return `0` on success, or a negative error code.
 */
WWMK_API int wwmk_get_virtual_space(WWMK_Rect *out);

/**
 * @brief Computes the center point of a rectangle.
 * @param rect Rectangle to inspect.
 * @return Center point of @p rect.
 */
WWMK_API WWMK_Point wwmk_rect_center(WWMK_Rect rect);

#ifdef __cplusplus
}
#endif
