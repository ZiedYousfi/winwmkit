/** @file winwmkit.c
 *  @brief Public Win32-facing implementation plus the async facade built on the internal worker.
 */

#include "winwmkit/winwmkit.h"
#include "wwmk_event_loop.h"

#include <ShObjIdl.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define WWMK_ALIGNOF(type)                                                     \
  offsetof(                                                                    \
      struct {                                                                 \
        char c;                                                                \
        type value;                                                            \
      },                                                                       \
      value)

enum {
  WWMK_STATUS_INVALID_ARGUMENT = -1,
  WWMK_STATUS_OUT_OF_MEMORY = -2,
  WWMK_STATUS_ENUM_FAILED = -3,
  WWMK_STATUS_NOT_FOUND = -4,
  WWMK_STATUS_ALREADY_RUNNING = -5,
  WWMK_STATUS_NOT_RUNNING = -6,
  WWMK_STATUS_THREAD_FAILED = -7
};

typedef struct {
  WWMK_EventCallback callback;
  void *userdata;
} WWMK_CallbackSlot;

typedef struct {
  WWMK_ActionCallback callback;
  void *userdata;
} WWMK_ActionCallbackSlot;

typedef struct {
  CRITICAL_SECTION lock;
  bool loop_initialized;
  WWMK_EventLoop loop;
  WWMK_PipeServer *pipe_server;
  WWMK_CallbackSlot generic_callback;
  WWMK_CallbackSlot window_created_callback;
  WWMK_CallbackSlot window_destroyed_callback;
  WWMK_CallbackSlot window_moved_callback;
  WWMK_CallbackSlot pipe_message_callback;
  WWMK_CallbackSlot monitor_changed_callback;
  WWMK_ActionCallbackSlot action_callback;
} WWMK_GlobalState;

static INIT_ONCE wwmk_state_once = INIT_ONCE_STATIC_INIT;
static WWMK_GlobalState wwmk_state = {0};

/** @brief Parses supported pipe commands into public actions when the payload is well formed. */
static int wwmk_try_parse_pipe_action(char *message, WWMK_Action *out);

/** @brief One-time initialization hook for the global process-local library state. */
static BOOL CALLBACK wwmk_state_init_once(PINIT_ONCE init_once, PVOID parameter,
                                          PVOID *context) {
  (void)init_once;
  (void)parameter;
  (void)context;

  InitializeCriticalSection(&wwmk_state.lock);
  return TRUE;
}

/** @brief Returns the lazily initialized global state shared by the public API. */
static WWMK_GlobalState *wwmk_get_global_state(void) {
  (void)InitOnceExecuteOnce(&wwmk_state_once, wwmk_state_init_once, NULL, NULL);
  return &wwmk_state;
}

/** @brief Routes internal worker events to the generic and type-specific public callbacks. */
static void wwmk_dispatch_internal_event(const WWMK_InternalEvent *event,
                                         void *userdata) {
  WWMK_GlobalState *state = (WWMK_GlobalState *)userdata;
  WWMK_Event public_event = {0};
  WWMK_CallbackSlot generic_callback = {0};
  WWMK_CallbackSlot specific_callback = {0};

  if (state == NULL || event == NULL) {
    return;
  }

  if (wwmk_event_loop_translate_event(event, &public_event) < 0) {
    return;
  }

  EnterCriticalSection(&state->lock);
  generic_callback = state->generic_callback;

  switch (public_event.type) {
  case WWMK_EVENT_WINDOW_CREATED:
    specific_callback = state->window_created_callback;
    break;
  case WWMK_EVENT_WINDOW_DESTROYED:
    specific_callback = state->window_destroyed_callback;
    break;
  case WWMK_EVENT_WINDOW_MOVED:
    specific_callback = state->window_moved_callback;
    break;
  case WWMK_EVENT_PIPE_MESSAGE:
    specific_callback = state->pipe_message_callback;
    break;
  case WWMK_EVENT_MONITOR_CHANGED:
    specific_callback = state->monitor_changed_callback;
    break;
  default:
    break;
  }
  LeaveCriticalSection(&state->lock);

  if (generic_callback.callback != NULL) {
    generic_callback.callback(&public_event, generic_callback.userdata);
  }

  if (specific_callback.callback != NULL) {
    specific_callback.callback(&public_event, specific_callback.userdata);
  }
}

/** @brief Pipe callback that both republishes the raw message and optionally turns it into an action. */
static void wwmk_handle_pipe_message(const char *message, size_t size,
                                     void *userdata) {
  WWMK_GlobalState *state = (WWMK_GlobalState *)userdata;
  char buffer[512] = {0};
  WWMK_Action action = {0};
  int parsed = 0;

  if (state == NULL || message == NULL) {
    return;
  }

  (void)wwmk_event_loop_post_pipe_message(&state->loop, message, size);

  if (size == 0 || size >= sizeof(buffer)) {
    return;
  }

  memcpy(buffer, message, size);
  buffer[size] = '\0';
  parsed = wwmk_try_parse_pipe_action(buffer, &action);
  if (parsed == 1) {
    (void)wwmk_post_action(&action, NULL, NULL);
  }
}

/** @brief Trims ASCII whitespace in-place before command parsing. */
static char *wwmk_trim_ascii(char *text) {
  char *end = NULL;

  if (text == NULL) {
    return NULL;
  }

  while (*text != '\0' && isspace((unsigned char)*text)) {
    text++;
  }

  if (*text == '\0') {
    return text;
  }

  end = text + strlen(text) - 1;
  while (end > text && isspace((unsigned char)*end)) {
    *end = '\0';
    end--;
  }

  return text;
}

/** @brief Parses a pipe token that represents an `HWND` printed with `%p`. */
static int wwmk_parse_window_handle_token(const char *token, WWMK_Window *out) {
  unsigned long long value = 0;
  char *end = NULL;

  if (token == NULL || out == NULL) {
    return 0;
  }

  value = strtoull(token, &end, 0);
  if (end == token || *end != '\0') {
    return 0;
  }

  memset(out, 0, sizeof(*out));
  out->hwnd = (void *)(uintptr_t)value;
  return out->hwnd != NULL ? 1 : 0;
}

/** @brief Maps textual pipe commands to the same public actions available through the API. */
static int wwmk_try_parse_pipe_action(char *message, WWMK_Action *out) {
  char *command = NULL;
  char *context = NULL;
  char *token = NULL;
  WWMK_Window window = {0};
  int a = 0;
  int b = 0;
  int c = 0;
  int d = 0;

  if (message == NULL || out == NULL) {
    return 0;
  }

  memset(out, 0, sizeof(*out));
  command = wwmk_trim_ascii(message);
  if (command == NULL || *command == '\0') {
    return 0;
  }

  token = strtok_s(command, " \t\r\n", &context);
  if (token == NULL) {
    return 0;
  }

  if (strcmp(token, "windows") == 0 || strcmp(token, "get_windows") == 0) {
    out->type = WWMK_ACTION_GET_WINDOWS;
    return 1;
  }

  if (strcmp(token, "monitors") == 0 || strcmp(token, "get_monitors") == 0) {
    out->type = WWMK_ACTION_GET_MONITORS;
    return 1;
  }

  if (strcmp(token, "move") == 0) {
    char *hwnd_token = strtok_s(NULL, " \t\r\n", &context);
    char *x_token = strtok_s(NULL, " \t\r\n", &context);
    char *y_token = strtok_s(NULL, " \t\r\n", &context);

    if (!wwmk_parse_window_handle_token(hwnd_token, &window) ||
        x_token == NULL || y_token == NULL || sscanf_s(x_token, "%d", &a) != 1 ||
        sscanf_s(y_token, "%d", &b) != 1) {
      return 0;
    }

    out->type = WWMK_ACTION_MOVE_WINDOW;
    out->data.move_window.window = window;
    out->data.move_window.x = a;
    out->data.move_window.y = b;
    return 1;
  }

  if (strcmp(token, "resize") == 0) {
    char *hwnd_token = strtok_s(NULL, " \t\r\n", &context);
    char *width_token = strtok_s(NULL, " \t\r\n", &context);
    char *height_token = strtok_s(NULL, " \t\r\n", &context);

    if (!wwmk_parse_window_handle_token(hwnd_token, &window) ||
        width_token == NULL || height_token == NULL ||
        sscanf_s(width_token, "%d", &a) != 1 ||
        sscanf_s(height_token, "%d", &b) != 1) {
      return 0;
    }

    out->type = WWMK_ACTION_RESIZE_WINDOW;
    out->data.resize_window.window = window;
    out->data.resize_window.width = a;
    out->data.resize_window.height = b;
    return 1;
  }

  if (strcmp(token, "set_rect") == 0 || strcmp(token, "set-window-rect") == 0) {
    char *hwnd_token = strtok_s(NULL, " \t\r\n", &context);
    char *x_token = strtok_s(NULL, " \t\r\n", &context);
    char *y_token = strtok_s(NULL, " \t\r\n", &context);
    char *width_token = strtok_s(NULL, " \t\r\n", &context);
    char *height_token = strtok_s(NULL, " \t\r\n", &context);

    if (!wwmk_parse_window_handle_token(hwnd_token, &window) ||
        x_token == NULL || y_token == NULL || width_token == NULL ||
        height_token == NULL || sscanf_s(x_token, "%d", &a) != 1 ||
        sscanf_s(y_token, "%d", &b) != 1 ||
        sscanf_s(width_token, "%d", &c) != 1 ||
        sscanf_s(height_token, "%d", &d) != 1) {
      return 0;
    }

    out->type = WWMK_ACTION_SET_WINDOW_RECT;
    out->data.set_window_rect.window = window;
    out->data.set_window_rect.rect = (WWMK_Rect){a, b, c, d};
    return 1;
  }

  if (strcmp(token, "get_window_rect") == 0) {
    char *hwnd_token = strtok_s(NULL, " \t\r\n", &context);

    if (!wwmk_parse_window_handle_token(hwnd_token, &window)) {
      return 0;
    }

    out->type = WWMK_ACTION_GET_WINDOW_RECT;
    out->data.get_window_rect.window = window;
    return 1;
  }

  if (strcmp(token, "monitor_from_window") == 0) {
    char *hwnd_token = strtok_s(NULL, " \t\r\n", &context);

    if (!wwmk_parse_window_handle_token(hwnd_token, &window)) {
      return 0;
    }

    out->type = WWMK_ACTION_MONITOR_FROM_WINDOW;
    out->data.monitor_from_window.window = window;
    return 1;
  }

  if (strcmp(token, "window_primary_monitor") == 0) {
    char *hwnd_token = strtok_s(NULL, " \t\r\n", &context);

    if (!wwmk_parse_window_handle_token(hwnd_token, &window)) {
      return 0;
    }

    out->type = WWMK_ACTION_WINDOW_PRIMARY_MONITOR;
    out->data.window_primary_monitor.window = window;
    return 1;
  }

  if (strcmp(token, "window_monitor_by_center") == 0) {
    char *hwnd_token = strtok_s(NULL, " \t\r\n", &context);

    if (!wwmk_parse_window_handle_token(hwnd_token, &window)) {
      return 0;
    }

    out->type = WWMK_ACTION_WINDOW_MONITOR_BY_CENTER;
    out->data.window_monitor_by_center.window = window;
    return 1;
  }

  return 0;
}

/** @brief Converts a Win32 `RECT` to the public rectangle shape. */
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

/** @brief Computes area using a widened type to avoid overflow while choosing monitors. */
static long long wwmk_rect_area_ll(WWMK_Rect rect) {
  if (rect.width <= 0 || rect.height <= 0) {
    return 0;
  }

  return (long long)rect.width * (long long)rect.height;
}

/** @brief Fills one public monitor snapshot from an `HMONITOR`. */
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

/** @brief Internal monitor enumeration callback that grows a temporary array. */
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

/** @brief Shared monitor enumeration helper used by direct and queued APIs. */
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
  WWMK_GlobalState *state = wwmk_get_global_state();

  EnterCriticalSection(&state->lock);
  state->generic_callback.callback = callback;
  state->generic_callback.userdata = userdata;
  LeaveCriticalSection(&state->lock);
  return 0;
}

int wwmk_set_action_callback(WWMK_ActionCallback callback, void *userdata) {
  WWMK_GlobalState *state = wwmk_get_global_state();

  EnterCriticalSection(&state->lock);
  state->action_callback.callback = callback;
  state->action_callback.userdata = userdata;
  LeaveCriticalSection(&state->lock);
  return 0;
}

int wwmk_start(const WWMK_StartOptions *options) {
  WWMK_GlobalState *state = wwmk_get_global_state();
  WWMK_StartOptions default_options = {0};
  int status = 0;

  if (options == NULL) {
    options = &default_options;
  }

  EnterCriticalSection(&state->lock);
  if (state->loop_initialized) {
    LeaveCriticalSection(&state->lock);
    return WWMK_STATUS_ALREADY_RUNNING;
  }
  LeaveCriticalSection(&state->lock);

  status =
      wwmk_event_loop_init(&state->loop, wwmk_dispatch_internal_event, state);
  if (status < 0) {
    return status;
  }

  status = wwmk_event_loop_start(&state->loop);
  if (status < 0) {
    wwmk_event_loop_destroy(&state->loop);
    return status;
  }

  EnterCriticalSection(&state->lock);
  state->loop_initialized = true;
  LeaveCriticalSection(&state->lock);

  if (options->pipe_name != NULL && options->pipe_name[0] != '\0') {
    WWMK_PipeServer *pipe_server =
        wwmk_pipe_server_start(options->pipe_name, wwmk_handle_pipe_message,
                               state);
    if (pipe_server == NULL) {
      EnterCriticalSection(&state->lock);
      state->loop_initialized = false;
      LeaveCriticalSection(&state->lock);
      (void)wwmk_event_loop_stop(&state->loop);
      wwmk_event_loop_destroy(&state->loop);
      return WWMK_STATUS_THREAD_FAILED;
    }

    EnterCriticalSection(&state->lock);
    state->pipe_server = pipe_server;
    LeaveCriticalSection(&state->lock);
  }

  return 0;
}

int wwmk_stop(void) {
  WWMK_GlobalState *state = wwmk_get_global_state();
  WWMK_PipeServer *pipe_server = NULL;
  int loop_initialized = 0;

  EnterCriticalSection(&state->lock);
  pipe_server = state->pipe_server;
  state->pipe_server = NULL;
  loop_initialized = state->loop_initialized ? 1 : 0;
  state->loop_initialized = false;
  LeaveCriticalSection(&state->lock);

  if (pipe_server != NULL) {
    (void)wwmk_pipe_server_stop(pipe_server);
  }

  if (loop_initialized) {
    (void)wwmk_event_loop_stop(&state->loop);
    wwmk_event_loop_destroy(&state->loop);
  }

  return 0;
}

int wwmk_on_window_created(WWMK_EventCallback callback, void *userdata) {
  WWMK_GlobalState *state = wwmk_get_global_state();

  EnterCriticalSection(&state->lock);
  state->window_created_callback.callback = callback;
  state->window_created_callback.userdata = userdata;
  LeaveCriticalSection(&state->lock);
  return 0;
}

int wwmk_on_window_destroyed(WWMK_EventCallback callback, void *userdata) {
  WWMK_GlobalState *state = wwmk_get_global_state();

  EnterCriticalSection(&state->lock);
  state->window_destroyed_callback.callback = callback;
  state->window_destroyed_callback.userdata = userdata;
  LeaveCriticalSection(&state->lock);
  return 0;
}

int wwmk_on_window_moved(WWMK_EventCallback callback, void *userdata) {
  WWMK_GlobalState *state = wwmk_get_global_state();

  EnterCriticalSection(&state->lock);
  state->window_moved_callback.callback = callback;
  state->window_moved_callback.userdata = userdata;
  LeaveCriticalSection(&state->lock);
  return 0;
}

int wwmk_on_pipe_message(WWMK_EventCallback callback, void *userdata) {
  WWMK_GlobalState *state = wwmk_get_global_state();

  EnterCriticalSection(&state->lock);
  state->pipe_message_callback.callback = callback;
  state->pipe_message_callback.userdata = userdata;
  LeaveCriticalSection(&state->lock);
  return 0;
}

int wwmk_on_monitor_changed(WWMK_EventCallback callback, void *userdata) {
  WWMK_GlobalState *state = wwmk_get_global_state();

  EnterCriticalSection(&state->lock);
  state->monitor_changed_callback.callback = callback;
  state->monitor_changed_callback.userdata = userdata;
  LeaveCriticalSection(&state->lock);
  return 0;
}

int wwmk_post_action(const WWMK_Action *action, WWMK_ActionCallback callback,
                     void *userdata) {
  WWMK_GlobalState *state = wwmk_get_global_state();
  WWMK_ActionCallbackSlot default_callback = {0};

  EnterCriticalSection(&state->lock);
  if (!state->loop_initialized) {
    LeaveCriticalSection(&state->lock);
    return WWMK_STATUS_NOT_RUNNING;
  }
  default_callback = state->action_callback;
  LeaveCriticalSection(&state->lock);

  if (callback == NULL) {
    callback = default_callback.callback;
    userdata = default_callback.userdata;
  }

  return wwmk_event_loop_post_action(&state->loop, action, callback, userdata);
}

/** @brief Uses the default action callback so fire-and-forget public APIs stay concise. */
static int wwmk_submit_simple_action(const WWMK_Action *action) {
  return wwmk_post_action(action, NULL, NULL);
}

int wwmk_request_windows(WWMK_ActionCallback callback, void *userdata) {
  WWMK_Action action = {0};

  action.type = WWMK_ACTION_GET_WINDOWS;
  return wwmk_post_action(&action, callback, userdata);
}

int wwmk_request_monitors(WWMK_ActionCallback callback, void *userdata) {
  WWMK_Action action = {0};

  action.type = WWMK_ACTION_GET_MONITORS;
  return wwmk_post_action(&action, callback, userdata);
}

int wwmk_request_window_rect(WWMK_Window window, WWMK_ActionCallback callback,
                             void *userdata) {
  WWMK_Action action = {0};

  action.type = WWMK_ACTION_GET_WINDOW_RECT;
  action.data.get_window_rect.window = window;
  return wwmk_post_action(&action, callback, userdata);
}

int wwmk_request_monitor_from_window(WWMK_Window window,
                                     WWMK_ActionCallback callback,
                                     void *userdata) {
  WWMK_Action action = {0};

  action.type = WWMK_ACTION_MONITOR_FROM_WINDOW;
  action.data.monitor_from_window.window = window;
  return wwmk_post_action(&action, callback, userdata);
}

int wwmk_request_window_primary_monitor(WWMK_Window window,
                                        WWMK_ActionCallback callback,
                                        void *userdata) {
  WWMK_Action action = {0};

  action.type = WWMK_ACTION_WINDOW_PRIMARY_MONITOR;
  action.data.window_primary_monitor.window = window;
  return wwmk_post_action(&action, callback, userdata);
}

int wwmk_request_window_monitor_by_center(WWMK_Window window,
                                          WWMK_ActionCallback callback,
                                          void *userdata) {
  WWMK_Action action = {0};

  action.type = WWMK_ACTION_WINDOW_MONITOR_BY_CENTER;
  action.data.window_monitor_by_center.window = window;
  return wwmk_post_action(&action, callback, userdata);
}

int wwmk_internal_get_monitors_direct(WWMK_Monitor *out, int cap) {
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

/** @brief Internal window enumeration callback that captures live Win32 window state. */
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
  (void)wwmk_internal_get_window_rect_direct(window, &window.rect);

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

int wwmk_internal_get_windows_direct(WWMK_Window **out, int cap) {
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

int wwmk_internal_get_window_rect_direct(WWMK_Window window, WWMK_Rect *out) {
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

int wwmk_internal_set_window_rect_direct(WWMK_Window window, WWMK_Rect rect) {
  if (window.hwnd == NULL) {
    return -1;
  }

  if (!MoveWindow((HWND)window.hwnd, rect.x, rect.y, rect.width, rect.height,
                  TRUE)) {
    return -2;
  }

  return 0;
}

WWMK_Monitor wwmk_internal_monitor_from_window_direct(WWMK_Window window) {
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

int wwmk_internal_window_primary_monitor_direct(WWMK_Window window,
                                                WWMK_Monitor *out) {
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
    (void)wwmk_internal_get_window_rect_direct(window, &window_rect);
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
    fallback_monitor = wwmk_internal_monitor_from_window_direct(window);
    if (fallback_monitor.id != 0) {
      *out = fallback_monitor;
      free(monitors);
      return 0;
    }
  }

  free(monitors);
  return WWMK_STATUS_NOT_FOUND;
}

int wwmk_internal_window_monitor_by_center_direct(WWMK_Window window,
                                                  WWMK_Monitor *out) {
  WWMK_Rect window_rect = window.rect;
  WWMK_Point center = {0};
  POINT win32_center = {0};
  HMONITOR monitor = NULL;

  if (out == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  *out = (WWMK_Monitor){0};

  if (window.hwnd != NULL) {
    (void)wwmk_internal_get_window_rect_direct(window, &window_rect);
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

int wwmk_get_monitors(WWMK_Monitor *out, int cap) {
  return wwmk_internal_get_monitors_direct(out, cap);
}

int wwmk_get_windows(WWMK_Window **out, int cap) {
  return wwmk_internal_get_windows_direct(out, cap);
}

int wwmk_get_window_rect(WWMK_Window window, WWMK_Rect *out) {
  return wwmk_internal_get_window_rect_direct(window, out);
}

int wwmk_set_window_rect(WWMK_Window window, WWMK_Rect rect) {
  WWMK_Action action = {0};

  action.type = WWMK_ACTION_SET_WINDOW_RECT;
  action.data.set_window_rect.window = window;
  action.data.set_window_rect.rect = rect;
  return wwmk_submit_simple_action(&action);
}

int wwmk_move_window(WWMK_Window window, int x, int y) {
  WWMK_Action action = {0};

  action.type = WWMK_ACTION_MOVE_WINDOW;
  action.data.move_window.window = window;
  action.data.move_window.x = x;
  action.data.move_window.y = y;
  return wwmk_submit_simple_action(&action);
}

int wwmk_resize_window(WWMK_Window window, int width, int height) {
  WWMK_Action action = {0};

  action.type = WWMK_ACTION_RESIZE_WINDOW;
  action.data.resize_window.window = window;
  action.data.resize_window.width = width;
  action.data.resize_window.height = height;
  return wwmk_submit_simple_action(&action);
}

WWMK_Monitor wwmk_monitor_from_window(WWMK_Window window) {
  return wwmk_internal_monitor_from_window_direct(window);
}

int wwmk_window_primary_monitor(WWMK_Window window, WWMK_Monitor *out) {
  return wwmk_internal_window_primary_monitor_direct(window, out);
}

int wwmk_window_monitor_by_center(WWMK_Window window, WWMK_Monitor *out) {
  return wwmk_internal_window_monitor_by_center_direct(window, out);
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
