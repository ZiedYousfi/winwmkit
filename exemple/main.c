#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "winwmkit/winwmkit.h"

/*
 * Small demonstration program:
 * - starts the library's asynchronous loop
 * - loads the window list through a callback-based request
 * - moves one window through the public API
 * - resizes the same window through a text command sent to the pipe
 */
typedef struct {
  HANDLE done_event;
  const char *pipe_name;
  LONG pending_actions;
  int saw_target_window;
} ExampleState;

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

static void example_mark_action_started(ExampleState *state) {
  if (state == NULL) {
    return;
  }

  InterlockedIncrement(&state->pending_actions);
}

static void example_mark_action_finished(ExampleState *state) {
  if (state == NULL) {
    return;
  }

  if (InterlockedDecrement(&state->pending_actions) <= 0) {
    SetEvent(state->done_event);
  }
}

static int send_pipe_command(const char *pipe_name, const char *command) {
  char full_name[256] = {0};
  HANDLE pipe = INVALID_HANDLE_VALUE;
  DWORD bytes_written = 0;

  if (pipe_name == NULL || command == NULL) {
    return 0;
  }

  snprintf(full_name, sizeof(full_name), "\\\\.\\pipe\\%s", pipe_name);
  if (!WaitNamedPipeA(full_name, 2000)) {
    return 0;
  }

  pipe = CreateFileA(full_name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  if (pipe == INVALID_HANDLE_VALUE) {
    return 0;
  }

  if (!WriteFile(pipe, command, (DWORD)strlen(command), &bytes_written, NULL)) {
    CloseHandle(pipe);
    return 0;
  }

  CloseHandle(pipe);
  return 1;
}

/* Shows the pipe event side: the raw message arrives here before, or alongside,
 * its optional transformation into an action. */
static void on_pipe_message(const WWMK_Event *event, void *userdata) {
  ExampleState *state = (ExampleState *)userdata;

  (void)state;
  if (event == NULL || event->type != WWMK_EVENT_PIPE_MESSAGE) {
    return;
  }

  printf("pipe event => \"%.*s\"\n", (int)event->message_size, event->message);
}

/* Shows the higher-level event side: a completed move action also emits a
 * public window-moved event. */
static void on_window_moved(const WWMK_Event *event, void *userdata) {
  (void)userdata;

  if (event == NULL || event->type != WWMK_EVENT_WINDOW_MOVED) {
    return;
  }

  printf("window moved event => hwnd=%p\n", (void *)event->window_id);
}

static const char *action_name(WWMK_ActionType type) {
  switch (type) {
  case WWMK_ACTION_GET_WINDOWS:
    return "GET_WINDOWS";
  case WWMK_ACTION_GET_MONITORS:
    return "GET_MONITORS";
  case WWMK_ACTION_GET_WINDOW_RECT:
    return "GET_WINDOW_RECT";
  case WWMK_ACTION_SET_WINDOW_RECT:
    return "SET_WINDOW_RECT";
  case WWMK_ACTION_MOVE_WINDOW:
    return "MOVE_WINDOW";
  case WWMK_ACTION_RESIZE_WINDOW:
    return "RESIZE_WINDOW";
  case WWMK_ACTION_MONITOR_FROM_WINDOW:
    return "MONITOR_FROM_WINDOW";
  case WWMK_ACTION_WINDOW_PRIMARY_MONITOR:
    return "WINDOW_PRIMARY_MONITOR";
  case WWMK_ACTION_WINDOW_MONITOR_BY_CENTER:
    return "WINDOW_MONITOR_BY_CENTER";
  default:
    return "UNKNOWN";
  }
}

static void on_default_action(const WWMK_ActionResult *result, void *userdata) {
  ExampleState *state = (ExampleState *)userdata;

  if (result == NULL) {
    return;
  }

  printf("action result => %s status=%d\n", action_name(result->type),
         result->status);

  switch (result->type) {
  case WWMK_ACTION_SET_WINDOW_RECT:
  case WWMK_ACTION_MOVE_WINDOW:
  case WWMK_ACTION_RESIZE_WINDOW:
  case WWMK_ACTION_GET_WINDOW_RECT:
    if (result->status == 0) {
      print_rect("  rect=", result->data.rect.rect);
    }
    break;
  case WWMK_ACTION_MONITOR_FROM_WINDOW:
  case WWMK_ACTION_WINDOW_PRIMARY_MONITOR:
  case WWMK_ACTION_WINDOW_MONITOR_BY_CENTER:
    if (result->status == 0) {
      printf("  monitor id=%p is_primary=%d\n",
             (void *)result->data.monitor.monitor.id,
             result->data.monitor.monitor.is_primary);
    }
    break;
  default:
    break;
  }

  if (result->type != WWMK_ACTION_GET_WINDOWS &&
      result->type != WWMK_ACTION_GET_MONITORS) {
    example_mark_action_finished(state);
  }
}

/*
 * This callback receives the window list from the queue.
 * It picks a "safe" window, then triggers:
 * - one direct action through the public API
 * - one equivalent action through the pipe
 */
static void on_windows_loaded(const WWMK_ActionResult *result, void *userdata) {
  ExampleState *state = (ExampleState *)userdata;
  int i = 0;

  if (result == NULL || state == NULL) {
    return;
  }

  printf("initial async window load => status=%d count=%d\n", result->status,
         result->status == 0 ? result->data.windows.count : 0);

  if (result->status != 0) {
    SetEvent(state->done_event);
    return;
  }

  for (i = 0; i < result->data.windows.count; i++) {
    const WWMK_Window *window = &result->data.windows.items[i];
    const char *reason = shuffle_skip_reason(window);
    char pipe_command[256] = {0};
    int enqueue_status = 0;

    if (reason != NULL) {
      continue;
    }

    printf("selected window => hwnd=%p title=\"%s\"\n", window->hwnd,
           window_title(window));
    print_rect("  current_rect=", window->rect);

    state->saw_target_window = 1;

    /* Action 1: the user calls the public API directly. */
    example_mark_action_started(state);
    enqueue_status = wwmk_move_window(*window, window->rect.x + 48,
                                      window->rect.y + 32);
    printf("wwmk_move_window(...) => %d\n", enqueue_status);
    if (enqueue_status != 0) {
      example_mark_action_finished(state);
    }

    snprintf(pipe_command, sizeof(pipe_command), "resize %p %d %d", window->hwnd,
             window->rect.width > 160 ? window->rect.width - 80
                                      : window->rect.width + 80,
             window->rect.height);

    /* Action 2: another process could send this command through the pipe.
     * The library receives the text, emits the pipe event, then parses it into
     * `WWMK_ACTION_RESIZE_WINDOW`. */
    example_mark_action_started(state);
    if (!send_pipe_command(state->pipe_name, pipe_command)) {
      printf("pipe write failed => %s\n", pipe_command);
      example_mark_action_finished(state);
    } else {
      printf("pipe write => %s\n", pipe_command);
    }

    return;
  }

  printf("No eligible windows found for the async demo.\n");
  SetEvent(state->done_event);
}

int main(void) {
  ExampleState state = {0};
  WWMK_StartOptions options = {0};
  int status = 0;
  DWORD wait_result = WAIT_OBJECT_0;

  state.pipe_name = "winwmkit-example";
  state.done_event = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (state.done_event == NULL) {
    fprintf(stderr, "Failed to create completion event.\n");
    return 1;
  }

  options.pipe_name = state.pipe_name;

  printf("WinWMKit async example.\n");
  printf("It queues one direct move action and one pipe-driven resize action.\n");

  /* Default action callback:
   * all fire-and-forget actions in the example report here. */
  status = wwmk_set_action_callback(on_default_action, &state);
  printf("wwmk_set_action_callback(...) => %d\n", status);

  /* Dedicated callback used to inspect the raw message coming from the pipe. */
  status = wwmk_on_pipe_message(on_pipe_message, &state);
  printf("wwmk_on_pipe_message(...) => %d\n", status);

  /* Additional event callback that shows how a successful action can also emit
   * a public event. */
  status = wwmk_on_window_moved(on_window_moved, &state);
  printf("wwmk_on_window_moved(...) => %d\n", status);

  /* Starts the loop and the optional pipe described in `options`. */
  status = wwmk_start(&options);
  printf("wwmk_start(&options) => %d\n", status);
  if (status != 0) {
    CloseHandle(state.done_event);
    return 1;
  }

  /* First real asynchronous request:
   * the response arrives in `on_windows_loaded`. */
  status = wwmk_request_windows(on_windows_loaded, &state);
  printf("wwmk_request_windows(...) => %d\n", status);
  if (status != 0) {
    (void)wwmk_stop();
    CloseHandle(state.done_event);
    return 1;
  }

  wait_result = WaitForSingleObject(state.done_event, 5000);
  printf("WaitForSingleObject(done_event, 5000) => %lu\n",
         (unsigned long)wait_result);

  if (!state.saw_target_window) {
    printf("No eligible window was moved during this run.\n");
  }

  status = wwmk_stop();
  printf("wwmk_stop() => %d\n", status);

  CloseHandle(state.done_event);
  return 0;
}
