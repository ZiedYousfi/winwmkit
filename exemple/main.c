#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "winwmkit/winwmkit.h"

typedef struct {
  const char *pipe_name;
  unsigned shuffle_round;
} ExampleState;

static void on_windows_loaded(const WWMK_ActionResult *result, void *userdata);

static const char *window_title(const WWMK_Window *window) {
  if (window == NULL || window->title[0] == '\0') {
    return "<untitled>";
  }

  return window->title;
}

/*
 * Keep the example on ordinary application windows so it demonstrates the
 * library API without dragging shell-specific Win32 filtering into user code.
 */
static int is_eligible_window(const WWMK_Window *window) {
  if (window == NULL || window->hwnd == NULL) {
    return 0;
  }

  if (!window->is_visible || window->is_minimized || window->is_maximized) {
    return 0;
  }

  if (window->rect.width <= 0 || window->rect.height <= 0) {
    return 0;
  }

  return window->title[0] != '\0';
}

static void shuffle_indices(int *indices, int count) {
  int i = 0;
  int identity = 1;

  if (indices == NULL || count <= 1) {
    return;
  }

  for (i = count - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int tmp = indices[i];
    indices[i] = indices[j];
    indices[j] = tmp;
  }

  for (i = 0; i < count; i++) {
    if (indices[i] != i) {
      identity = 0;
      break;
    }
  }

  if (identity) {
    int tmp = indices[0];
    indices[0] = indices[1];
    indices[1] = tmp;
  }
}

static void request_shuffle(ExampleState *state, const char *reason) {
  int status = 0;

  if (state == NULL) {
    return;
  }

  status = wwmk_request_windows(on_windows_loaded, state);
  printf("shuffle request (%s) => %d\n", reason, status);
}

static void on_pipe_message(const WWMK_Event *event, void *userdata) {
  ExampleState *state = (ExampleState *)userdata;

  if (event == NULL || event->type != WWMK_EVENT_PIPE_MESSAGE ||
      state == NULL) {
    return;
  }

  printf("pipe message => \"%.*s\"\n", (int)event->message_size,
         event->message);
  request_shuffle(state, "pipe");
}

static void on_windows_loaded(const WWMK_ActionResult *result, void *userdata) {
  ExampleState *state = (ExampleState *)userdata;
  WWMK_Window *eligible = NULL;
  WWMK_Rect *positions = NULL;
  int *indices = NULL;
  int eligible_count = 0;
  int i = 0;

  if (result == NULL || state == NULL) {
    return;
  }

  if (result->status != 0) {
    printf("window enumeration failed => %d\n", result->status);
    return;
  }

  if (result->data.windows.count == 0) {
    printf("shuffle skipped => no windows returned\n");
    return;
  }

  eligible = calloc((size_t)result->data.windows.count, sizeof(*eligible));
  positions = calloc((size_t)result->data.windows.count, sizeof(*positions));
  indices = calloc((size_t)result->data.windows.count, sizeof(*indices));
  if (eligible == NULL || positions == NULL || indices == NULL) {
    printf("shuffle skipped => out of memory\n");
    free(eligible);
    free(positions);
    free(indices);
    return;
  }

  for (i = 0; i < result->data.windows.count; i++) {
    const WWMK_Window *window = &result->data.windows.items[i];

    if (!is_eligible_window(window)) {
      continue;
    }

    eligible[eligible_count] = *window;
    positions[eligible_count] = window->rect;
    indices[eligible_count] = eligible_count;
    eligible_count++;
  }

  if (eligible_count < 2) {
    printf("shuffle skipped => need at least 2 eligible windows, found %d\n",
           eligible_count);
    free(eligible);
    free(positions);
    free(indices);
    return;
  }

  shuffle_indices(indices, eligible_count);
  state->shuffle_round++;

  printf("shuffle round %u => %d windows\n", state->shuffle_round,
         eligible_count);

  for (i = 0; i < eligible_count; i++) {
    const WWMK_Window *window = &eligible[i];
    WWMK_Rect target = positions[indices[i]];
    int status = 0;

    printf("  move \"%s\" => x=%d y=%d\n", window_title(window), target.x,
           target.y);

    status = wwmk_move_window(*window, target.x, target.y);
    if (status != 0) {
      printf("  move failed => hwnd=%p status=%d\n", window->hwnd, status);
    }
  }

  free(eligible);
  free(positions);
  free(indices);
}

int main(void) {
  ExampleState state = {0};
  WWMK_StartOptions options = {0};
  int status = 0;

  srand((unsigned)time(NULL));

  state.pipe_name = "winwmkit-example";
  options.pipe_name = state.pipe_name;

  printf("WinWMKit example.\n");
  printf(
      "It enumerates visible titled windows and shuffles their positions.\n");
  printf("Any pipe message reshuffles them again.\n");

  status = wwmk_on_pipe_message(on_pipe_message, &state);
  printf("wwmk_on_pipe_message(...) => %d\n", status);

  status = wwmk_start(&options);
  printf("wwmk_start(&options) => %d\n", status);
  if (status != 0) {
    return 1;
  }

  request_shuffle(&state, "startup");

  printf("Send any text to \\\\.\\pipe\\%s to reshuffle again.\n",
         state.pipe_name);
  printf("Press Enter to quit.\n");
  (void)getchar();

  status = wwmk_stop();
  printf("wwmk_stop() => %d\n", status);
  return status == 0 ? 0 : 1;
}
