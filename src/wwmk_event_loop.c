/** @file wwmk_event_loop.c
 *  @brief Internal MPSC queue and worker implementation for Win32 actions.
 */

#include "wwmk_event_loop.h"

#include <stdlib.h>
#include <string.h>

enum {
  WWMK_STATUS_INVALID_ARGUMENT = -1,
  WWMK_STATUS_OUT_OF_MEMORY = -2,
  WWMK_STATUS_ENUM_FAILED = -3,
  WWMK_STATUS_NOT_FOUND = -4,
  WWMK_STATUS_ALREADY_RUNNING = -5,
  WWMK_STATUS_NOT_RUNNING = -6,
  WWMK_STATUS_THREAD_FAILED = -7
};

static void wwmk_event_queue_item_free(WWMK_QueueItem *item) {
  if (item == NULL) {
    return;
  }

  free(item->message);
  free(item);
}

/** @brief Initializes the linked-list queue used by multiple producers. */
int wwmk_event_queue_init(WWMK_EventQueue *queue) {
  if (queue == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  memset(queue, 0, sizeof(*queue));
  InitializeCriticalSection(&queue->lock);
  InitializeConditionVariable(&queue->cv);
  return 0;
}

/** @brief Frees any queued work items left after the worker is stopped. */
void wwmk_event_queue_destroy(WWMK_EventQueue *queue) {
  WWMK_QueueItem *item = NULL;
  WWMK_QueueItem *next = NULL;

  if (queue == NULL) {
    return;
  }

  item = queue->head;
  while (item != NULL) {
    next = item->next;
    wwmk_event_queue_item_free(item);
    item = next;
  }

  queue->head = NULL;
  queue->tail = NULL;
  queue->closed = true;
  DeleteCriticalSection(&queue->lock);
}

/** @brief Appends one queue node while producers serialize through one lock. */
int wwmk_event_queue_push(WWMK_EventQueue *queue, WWMK_QueueItem *item) {
  if (queue == NULL || item == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  item->next = NULL;

  EnterCriticalSection(&queue->lock);
  if (queue->closed) {
    LeaveCriticalSection(&queue->lock);
    return WWMK_STATUS_NOT_RUNNING;
  }

  if (queue->tail != NULL) {
    queue->tail->next = item;
    queue->tail = item;
  } else {
    queue->head = item;
    queue->tail = item;
  }

  WakeConditionVariable(&queue->cv);
  LeaveCriticalSection(&queue->lock);
  return 0;
}

/** @brief Blocking pop used by the single worker thread. */
int wwmk_event_queue_pop_wait(WWMK_EventQueue *queue, WWMK_QueueItem **out) {
  WWMK_QueueItem *item = NULL;

  if (queue == NULL || out == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  EnterCriticalSection(&queue->lock);
  while (queue->head == NULL && !queue->closed) {
    SleepConditionVariableCS(&queue->cv, &queue->lock, INFINITE);
  }

  item = queue->head;
  if (item == NULL) {
    LeaveCriticalSection(&queue->lock);
    *out = NULL;
    return 0;
  }

  queue->head = item->next;
  if (queue->head == NULL) {
    queue->tail = NULL;
  }

  LeaveCriticalSection(&queue->lock);
  *out = item;
  return 1;
}

/** @brief Non-blocking pop kept for small internal probes and tests. */
int wwmk_event_queue_pop_nowait(WWMK_EventQueue *queue, WWMK_QueueItem **out) {
  WWMK_QueueItem *item = NULL;

  if (queue == NULL || out == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  EnterCriticalSection(&queue->lock);
  item = queue->head;
  if (item != NULL) {
    queue->head = item->next;
    if (queue->head == NULL) {
      queue->tail = NULL;
    }
  }
  LeaveCriticalSection(&queue->lock);

  *out = item;
  return item != NULL ? 1 : 0;
}

/** @brief Closes the queue so producers fail fast during shutdown. */
void wwmk_event_queue_close(WWMK_EventQueue *queue) {
  if (queue == NULL) {
    return;
  }

  EnterCriticalSection(&queue->lock);
  queue->closed = true;
  WakeAllConditionVariable(&queue->cv);
  LeaveCriticalSection(&queue->lock);
}

/** @brief Prepares worker state before the public layer exposes it. */
int wwmk_event_loop_init(WWMK_EventLoop *loop,
                         WWMK_InternalEventCallback callback,
                         void *userdata) {
  int status = 0;

  if (loop == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  memset(loop, 0, sizeof(*loop));
  InitializeCriticalSection(&loop->state_lock);

  status = wwmk_event_queue_init(&loop->queue);
  if (status < 0) {
    DeleteCriticalSection(&loop->state_lock);
    return status;
  }

  loop->callback = callback;
  loop->callback_userdata = userdata;
  return 0;
}

/** @brief Idempotent destruction helper used by the public stop path. */
void wwmk_event_loop_destroy(WWMK_EventLoop *loop) {
  if (loop == NULL) {
    return;
  }

  (void)wwmk_event_loop_stop(loop);
  wwmk_event_queue_destroy(&loop->queue);
  DeleteCriticalSection(&loop->state_lock);
  memset(loop, 0, sizeof(*loop));
}

/** @brief Starts the dedicated worker that owns all queued Win32 operations. */
int wwmk_event_loop_start(WWMK_EventLoop *loop) {
  if (loop == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  EnterCriticalSection(&loop->state_lock);
  if (loop->running) {
    LeaveCriticalSection(&loop->state_lock);
    return WWMK_STATUS_ALREADY_RUNNING;
  }

  loop->stop_requested = false;
  loop->worker_thread =
      CreateThread(NULL, 0, wwmk_event_loop_thread_main, loop, 0,
                   &loop->worker_thread_id);
  if (loop->worker_thread == NULL) {
    LeaveCriticalSection(&loop->state_lock);
    return WWMK_STATUS_THREAD_FAILED;
  }

  loop->running = true;
  LeaveCriticalSection(&loop->state_lock);
  return 0;
}

/** @brief Stops the worker, closes the queue, and waits for thread exit. */
int wwmk_event_loop_stop(WWMK_EventLoop *loop) {
  HANDLE worker_thread = NULL;

  if (loop == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  EnterCriticalSection(&loop->state_lock);
  if (!loop->running && loop->worker_thread == NULL) {
    LeaveCriticalSection(&loop->state_lock);
    return 0;
  }

  loop->stop_requested = true;
  worker_thread = loop->worker_thread;
  loop->worker_thread = NULL;
  loop->worker_thread_id = 0;
  loop->running = false;
  LeaveCriticalSection(&loop->state_lock);

  wwmk_event_queue_close(&loop->queue);

  if (worker_thread != NULL) {
    (void)WaitForSingleObject(worker_thread, INFINITE);
    CloseHandle(worker_thread);
  }

  return 0;
}

/** @brief Releases result buffers that are temporary by contract. */
static void wwmk_action_result_cleanup(WWMK_ActionResult *result) {
  if (result == NULL) {
    return;
  }

  switch (result->type) {
  case WWMK_ACTION_GET_WINDOWS:
    free(result->data.windows.items);
    result->data.windows.items = NULL;
    result->data.windows.count = 0;
    break;
  case WWMK_ACTION_GET_MONITORS:
    free(result->data.monitors.items);
    result->data.monitors.items = NULL;
    result->data.monitors.count = 0;
    break;
  default:
    break;
  }
}

/** @brief Emits derived move/resize events after a queued rectangle change completes. */
static void wwmk_event_loop_emit_window_rect_events(WWMK_EventLoop *loop,
                                                    uintptr_t window_id,
                                                    WWMK_Rect before,
                                                    WWMK_Rect after) {
  if (before.x != after.x || before.y != after.y) {
    WWMK_InternalEvent moved_event = {0};
    moved_event.type = WWMK_INTERNAL_EVENT_WINDOW_MOVED;
    moved_event.window_id = window_id;
    moved_event.rect = after;
    wwmk_event_loop_dispatch(loop, &moved_event);
  }

  if (before.width != after.width || before.height != after.height) {
    WWMK_InternalEvent resized_event = {0};
    resized_event.type = WWMK_INTERNAL_EVENT_WINDOW_RESIZED;
    resized_event.window_id = window_id;
    resized_event.rect = after;
    wwmk_event_loop_dispatch(loop, &resized_event);
  }
}

/** @brief Executes the queued “enumerate windows” action on the worker thread. */
static int wwmk_action_process_get_windows(WWMK_ActionResult *result) {
  WWMK_Window *windows = NULL;
  int count = 0;

  count = wwmk_internal_get_windows_direct(&windows, 0);
  if (count < 0) {
    result->status = count;
    return count;
  }

  result->status = 0;
  result->data.windows.items = windows;
  result->data.windows.count = count;
  return 0;
}

/** @brief Executes the queued “enumerate monitors” action on the worker thread. */
static int wwmk_action_process_get_monitors(WWMK_ActionResult *result) {
  WWMK_Monitor *monitors = NULL;
  int count = 0;
  int fetched = 0;

  count = wwmk_internal_get_monitors_direct(NULL, 0);
  if (count < 0) {
    result->status = count;
    return count;
  }

  if (count == 0) {
    result->status = 0;
    result->data.monitors.items = NULL;
    result->data.monitors.count = 0;
    return 0;
  }

  monitors = (WWMK_Monitor *)calloc((size_t)count, sizeof(*monitors));
  if (monitors == NULL) {
    result->status = WWMK_STATUS_OUT_OF_MEMORY;
    return result->status;
  }

  fetched = wwmk_internal_get_monitors_direct(monitors, count);
  if (fetched < 0) {
    free(monitors);
    result->status = fetched;
    return fetched;
  }

  result->status = 0;
  result->data.monitors.items = monitors;
  result->data.monitors.count = fetched;
  return 0;
}

/** @brief Executes the queued focused-window read on the worker thread. */
static int wwmk_action_process_get_focused_window(WWMK_ActionResult *result) {
  result->status = wwmk_internal_get_focused_window_direct(
      &result->data.focused_window.window);
  return result->status;
}

/** @brief Executes a queued rectangle read and writes the answer into the action result. */
static int wwmk_action_process_get_window_rect(const WWMK_Action *action,
                                               WWMK_ActionResult *result) {
  result->status =
      wwmk_internal_get_window_rect_direct(action->data.get_window_rect.window,
                                           &result->data.rect.rect);
  return result->status;
}

/** @brief Central rectangle mutation primitive used by set/move/resize actions. */
static int wwmk_action_process_set_window_rect(WWMK_EventLoop *loop,
                                               WWMK_Window window,
                                               WWMK_Rect target,
                                               WWMK_ActionResult *result) {
  WWMK_Rect before = {0};
  WWMK_Rect after = {0};
  int status = 0;

  status = wwmk_internal_get_window_rect_direct(window, &before);
  if (status < 0) {
    result->status = status;
    return status;
  }

  status = wwmk_internal_set_window_rect_direct(window, target);
  if (status < 0) {
    result->status = status;
    return status;
  }

  status = wwmk_internal_get_window_rect_direct(window, &after);
  if (status < 0) {
    after = target;
  }

  result->status = 0;
  result->data.rect.rect = after;
  wwmk_event_loop_emit_window_rect_events(loop, (uintptr_t)window.hwnd, before,
                                          after);
  return 0;
}

/** @brief Resolves queued monitor lookup actions that return one monitor. */
static int wwmk_action_process_monitor_result(WWMK_ActionType type,
                                              WWMK_Window window,
                                              WWMK_ActionResult *result) {
  switch (type) {
  case WWMK_ACTION_MONITOR_FROM_WINDOW:
    result->data.monitor.monitor = wwmk_internal_monitor_from_window_direct(window);
    if (result->data.monitor.monitor.id == 0) {
      result->status = WWMK_STATUS_NOT_FOUND;
      return result->status;
    }
    result->status = 0;
    return 0;
  case WWMK_ACTION_WINDOW_PRIMARY_MONITOR:
    result->status =
        wwmk_internal_window_primary_monitor_direct(
            window, &result->data.monitor.monitor);
    return result->status;
  case WWMK_ACTION_WINDOW_MONITOR_BY_CENTER:
    result->status =
        wwmk_internal_window_monitor_by_center_direct(
            window, &result->data.monitor.monitor);
    return result->status;
  default:
    result->status = WWMK_STATUS_INVALID_ARGUMENT;
    return result->status;
  }
}

/** @brief Switches on the public action type and invokes the matching direct helper. */
static void wwmk_event_loop_process_action(WWMK_EventLoop *loop,
                                           WWMK_QueueItem *item) {
  WWMK_ActionResult result = {0};
  WWMK_Rect target = {0};
  int status = 0;

  if (loop == NULL || item == NULL) {
    return;
  }

  result.type = item->action.type;
  result.status = WWMK_STATUS_INVALID_ARGUMENT;

  switch (item->action.type) {
  case WWMK_ACTION_GET_WINDOWS:
    status = wwmk_action_process_get_windows(&result);
    break;
  case WWMK_ACTION_GET_MONITORS:
    status = wwmk_action_process_get_monitors(&result);
    break;
  case WWMK_ACTION_GET_FOCUSED_WINDOW:
    status = wwmk_action_process_get_focused_window(&result);
    break;
  case WWMK_ACTION_GET_WINDOW_RECT:
    status = wwmk_action_process_get_window_rect(&item->action, &result);
    break;
  case WWMK_ACTION_SET_WINDOW_RECT:
    status = wwmk_action_process_set_window_rect(
        loop, item->action.data.set_window_rect.window,
        item->action.data.set_window_rect.rect, &result);
    break;
  case WWMK_ACTION_MOVE_WINDOW:
    status =
        wwmk_internal_get_window_rect_direct(
            item->action.data.move_window.window, &target);
    if (status == 0) {
      target.x = item->action.data.move_window.x;
      target.y = item->action.data.move_window.y;
      status = wwmk_action_process_set_window_rect(
          loop, item->action.data.move_window.window, target, &result);
    } else {
      result.status = status;
    }
    break;
  case WWMK_ACTION_RESIZE_WINDOW:
    status =
        wwmk_internal_get_window_rect_direct(
            item->action.data.resize_window.window, &target);
    if (status == 0) {
      target.width = item->action.data.resize_window.width;
      target.height = item->action.data.resize_window.height;
      status = wwmk_action_process_set_window_rect(
          loop, item->action.data.resize_window.window, target, &result);
    } else {
      result.status = status;
    }
    break;
  case WWMK_ACTION_MONITOR_FROM_WINDOW:
    status = wwmk_action_process_monitor_result(
        item->action.type, item->action.data.monitor_from_window.window,
        &result);
    break;
  case WWMK_ACTION_WINDOW_PRIMARY_MONITOR:
    status = wwmk_action_process_monitor_result(
        item->action.type, item->action.data.window_primary_monitor.window,
        &result);
    break;
  case WWMK_ACTION_WINDOW_MONITOR_BY_CENTER:
    status = wwmk_action_process_monitor_result(
        item->action.type, item->action.data.window_monitor_by_center.window,
        &result);
    break;
  default:
    status = WWMK_STATUS_INVALID_ARGUMENT;
    result.status = status;
    break;
  }

  if (item->action_callback != NULL) {
    item->action_callback(&result, item->action_userdata);
  }

  (void)status;
  wwmk_action_result_cleanup(&result);
}

/** @brief Copies one public action into a queue node for worker-side execution. */
int wwmk_event_loop_post_action(WWMK_EventLoop *loop, const WWMK_Action *action,
                                WWMK_ActionCallback callback, void *userdata) {
  WWMK_QueueItem *item = NULL;
  int status = 0;

  if (loop == NULL || action == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  EnterCriticalSection(&loop->state_lock);
  if (!loop->running || loop->stop_requested) {
    LeaveCriticalSection(&loop->state_lock);
    return WWMK_STATUS_NOT_RUNNING;
  }
  LeaveCriticalSection(&loop->state_lock);

  item = (WWMK_QueueItem *)calloc(1, sizeof(*item));
  if (item == NULL) {
    return WWMK_STATUS_OUT_OF_MEMORY;
  }

  item->type = WWMK_QUEUE_ITEM_ACTION;
  item->action = *action;
  item->action_callback = callback;
  item->action_userdata = userdata;

  status = wwmk_event_queue_push(&loop->queue, item);
  if (status < 0) {
    wwmk_event_queue_item_free(item);
    return status;
  }

  return 0;
}

/** @brief Copies one raw pipe payload into the queue so the worker serializes it too. */
int wwmk_event_loop_post_pipe_message(WWMK_EventLoop *loop, const char *message,
                                      size_t size) {
  WWMK_QueueItem *item = NULL;
  int status = 0;

  if (loop == NULL || message == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  EnterCriticalSection(&loop->state_lock);
  if (!loop->running || loop->stop_requested) {
    LeaveCriticalSection(&loop->state_lock);
    return WWMK_STATUS_NOT_RUNNING;
  }
  LeaveCriticalSection(&loop->state_lock);

  item = (WWMK_QueueItem *)calloc(1, sizeof(*item));
  if (item == NULL) {
    return WWMK_STATUS_OUT_OF_MEMORY;
  }

  item->message = (char *)malloc(size + 1u);
  if (item->message == NULL) {
    free(item);
    return WWMK_STATUS_OUT_OF_MEMORY;
  }

  memcpy(item->message, message, size);
  item->message[size] = '\0';
  item->message_size = size;
  item->type = WWMK_QUEUE_ITEM_PIPE_MESSAGE;

  status = wwmk_event_queue_push(&loop->queue, item);
  if (status < 0) {
    wwmk_event_queue_item_free(item);
    return status;
  }

  return 0;
}

/** @brief Single consumer loop that executes actions and republishes pipe events. */
DWORD WINAPI wwmk_event_loop_thread_main(LPVOID arg) {
  WWMK_EventLoop *loop = (WWMK_EventLoop *)arg;
  WWMK_QueueItem *item = NULL;
  int pop_status = 0;

  if (loop == NULL) {
    return 1;
  }

  for (;;) {
    pop_status = wwmk_event_queue_pop_wait(&loop->queue, &item);
    if (pop_status <= 0 || item == NULL) {
      break;
    }

    switch (item->type) {
    case WWMK_QUEUE_ITEM_ACTION:
      wwmk_event_loop_process_action(loop, item);
      break;
    case WWMK_QUEUE_ITEM_PIPE_MESSAGE: {
      WWMK_InternalEvent event = {0};
      event.type = WWMK_INTERNAL_EVENT_PIPE_MESSAGE;
      event.message = item->message;
      event.message_size = item->message_size;
      wwmk_event_loop_dispatch(loop, &event);
      break;
    }
    case WWMK_QUEUE_ITEM_QUIT:
      wwmk_event_queue_item_free(item);
      return 0;
    default:
      break;
    }

    wwmk_event_queue_item_free(item);
    item = NULL;
  }

  return 0;
}

/** @brief Converts internal event kinds to the public event enum and payload view. */
int wwmk_event_loop_translate_event(const WWMK_InternalEvent *internal_event,
                                    WWMK_Event *public_event) {
  if (internal_event == NULL || public_event == NULL) {
    return WWMK_STATUS_INVALID_ARGUMENT;
  }

  memset(public_event, 0, sizeof(*public_event));
  public_event->window_id = internal_event->window_id;
  public_event->monitor_id = internal_event->monitor_id;
  public_event->message = internal_event->message;
  public_event->message_size = internal_event->message_size;

  switch (internal_event->type) {
  case WWMK_INTERNAL_EVENT_WINDOW_CREATED:
    public_event->type = WWMK_EVENT_WINDOW_CREATED;
    return 0;
  case WWMK_INTERNAL_EVENT_WINDOW_DESTROYED:
    public_event->type = WWMK_EVENT_WINDOW_DESTROYED;
    return 0;
  case WWMK_INTERNAL_EVENT_WINDOW_MOVED:
    public_event->type = WWMK_EVENT_WINDOW_MOVED;
    return 0;
  case WWMK_INTERNAL_EVENT_WINDOW_RESIZED:
    public_event->type = WWMK_EVENT_WINDOW_RESIZED;
    return 0;
  case WWMK_INTERNAL_EVENT_WINDOW_FOCUSED:
    public_event->type = WWMK_EVENT_WINDOW_FOCUSED;
    return 0;
  case WWMK_INTERNAL_EVENT_PIPE_MESSAGE:
    public_event->type = WWMK_EVENT_PIPE_MESSAGE;
    return 0;
  case WWMK_INTERNAL_EVENT_MONITOR_ADDED:
  case WWMK_INTERNAL_EVENT_MONITOR_REMOVED:
  case WWMK_INTERNAL_EVENT_MONITOR_UPDATED:
    public_event->type = WWMK_EVENT_MONITOR_CHANGED;
    return 0;
  default:
    return WWMK_STATUS_NOT_FOUND;
  }
}

/** @brief Final bridge from worker events to the public dispatcher stored in `winwmkit.c`. */
void wwmk_event_loop_dispatch(WWMK_EventLoop *loop,
                              const WWMK_InternalEvent *event) {
  if (loop == NULL || event == NULL) {
    return;
  }

  if (loop->callback == NULL) {
    return;
  }

  if (wwmk_event_loop_translate_event(event, &(WWMK_Event){0}) < 0) {
    return;
  }

  loop->callback(event, loop->callback_userdata);
}
