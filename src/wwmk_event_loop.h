/** @file wwmk_event_loop.h
 *  @brief Internal event-loop and queue contracts used by the Win32 backend.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#include "winwmkit/winwmkit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  WWMK_INTERNAL_EVENT_NONE = 0,
  WWMK_INTERNAL_EVENT_WINDOW_CREATED,
  WWMK_INTERNAL_EVENT_WINDOW_DESTROYED,
  WWMK_INTERNAL_EVENT_WINDOW_MOVED,
  WWMK_INTERNAL_EVENT_WINDOW_RESIZED,
  WWMK_INTERNAL_EVENT_WINDOW_FOCUSED,
  WWMK_INTERNAL_EVENT_PIPE_MESSAGE,
  WWMK_INTERNAL_EVENT_MONITOR_ADDED,
  WWMK_INTERNAL_EVENT_MONITOR_REMOVED,
  WWMK_INTERNAL_EVENT_MONITOR_UPDATED
} WWMK_InternalEventType;

/** @brief Internal event format before translation to the public API. */
typedef struct {
  WWMK_InternalEventType type;
  uintptr_t window_id;
  uintptr_t monitor_id;
  WWMK_Rect rect;
  const char *message;
  size_t message_size;
} WWMK_InternalEvent;

/** @brief Bridge from the worker to the public event dispatcher. */
typedef void (*WWMK_InternalEventCallback)(const WWMK_InternalEvent *event,
                                           void *userdata);

typedef enum {
  WWMK_QUEUE_ITEM_NONE = 0,
  WWMK_QUEUE_ITEM_ACTION,
  WWMK_QUEUE_ITEM_PIPE_MESSAGE,
  WWMK_QUEUE_ITEM_QUIT
} WWMK_QueueItemType;

typedef struct WWMK_QueueItem WWMK_QueueItem;

/** @brief Linked-list node used by the MPSC action and pipe queue. */
struct WWMK_QueueItem {
  WWMK_QueueItemType type;
  WWMK_Action action;
  WWMK_ActionCallback action_callback;
  void *action_userdata;
  char *message;
  size_t message_size;
  WWMK_QueueItem *next;
};

/** @brief Single-consumer queue protected by a critical section and condition variable. */
typedef struct {
  WWMK_QueueItem *head;
  WWMK_QueueItem *tail;
  CRITICAL_SECTION lock;
  CONDITION_VARIABLE cv;
  bool closed;
} WWMK_EventQueue;

/** @brief Internal worker state for the asynchronous Win32 interaction layer. */
typedef struct {
  HANDLE worker_thread;
  DWORD worker_thread_id;
  CRITICAL_SECTION state_lock;
  bool running;
  bool stop_requested;
  WWMK_EventQueue queue;
  WWMK_InternalEventCallback callback;
  void *callback_userdata;
} WWMK_EventLoop;

/** @brief Initializes the internal queue backing the event loop. */
int wwmk_event_queue_init(WWMK_EventQueue *queue);
/** @brief Releases queue nodes left after the worker shuts down. */
void wwmk_event_queue_destroy(WWMK_EventQueue *queue);
/** @brief Pushes an action or pipe message into the internal MPSC queue. */
int wwmk_event_queue_push(WWMK_EventQueue *queue, WWMK_QueueItem *item);
/** @brief Waits for the next queue item on the worker thread. */
int wwmk_event_queue_pop_wait(WWMK_EventQueue *queue, WWMK_QueueItem **out);
/** @brief Attempts a non-blocking pop; used only by internal helpers. */
int wwmk_event_queue_pop_nowait(WWMK_EventQueue *queue, WWMK_QueueItem **out);
/** @brief Marks the queue closed and wakes the worker so stop can complete. */
void wwmk_event_queue_close(WWMK_EventQueue *queue);

/** @brief Initializes the event loop state before `wwmk_start()` publishes it. */
int wwmk_event_loop_init(WWMK_EventLoop *loop,
                         WWMK_InternalEventCallback callback, void *userdata);
/** @brief Tears down the internal loop and drains any remaining queue items. */
void wwmk_event_loop_destroy(WWMK_EventLoop *loop);
/** @brief Starts the single worker thread that owns Win32 side effects. */
int wwmk_event_loop_start(WWMK_EventLoop *loop);
/** @brief Stops the worker and closes the queue to reject late producers. */
int wwmk_event_loop_stop(WWMK_EventLoop *loop);
/** @brief Enqueues a public action for asynchronous execution. */
int wwmk_event_loop_post_action(WWMK_EventLoop *loop, const WWMK_Action *action,
                                WWMK_ActionCallback callback, void *userdata);
/** @brief Enqueues an incoming pipe payload so it shares the same serialization path. */
int wwmk_event_loop_post_pipe_message(WWMK_EventLoop *loop, const char *message,
                                      size_t size);
/** @brief Main worker entry point that serializes actions and pipe events. */
DWORD WINAPI wwmk_event_loop_thread_main(LPVOID arg);
/** @brief Maps internal events to the public `WWMK_Event` shape. */
int wwmk_event_loop_translate_event(const WWMK_InternalEvent *internal_event,
                                    WWMK_Event *public_event);
/** @brief Dispatches a translated event to the callback registered by `winwmkit.c`. */
void wwmk_event_loop_dispatch(WWMK_EventLoop *loop,
                              const WWMK_InternalEvent *event);

/** @brief Direct monitor enumeration used only by the worker implementation. */
int wwmk_internal_get_monitors_direct(WWMK_Monitor *out, int cap);
/** @brief Direct window enumeration used only by the worker implementation. */
int wwmk_internal_get_windows_direct(WWMK_Window **out, int cap);
/** @brief Direct focused-window lookup used behind queued actions. */
int wwmk_internal_get_focused_window_direct(WWMK_Window *out);
/** @brief Direct `GetWindowRect` wrapper used behind queued actions. */
int wwmk_internal_get_window_rect_direct(WWMK_Window window, WWMK_Rect *out);
/** @brief Direct `MoveWindow` wrapper used behind queued actions. */
int wwmk_internal_set_window_rect_direct(WWMK_Window window, WWMK_Rect rect);
/** @brief Direct monitor lookup used behind queued actions. */
WWMK_Monitor wwmk_internal_monitor_from_window_direct(WWMK_Window window);
/** @brief Direct “largest overlap” monitor lookup used behind queued actions. */
int wwmk_internal_window_primary_monitor_direct(WWMK_Window window,
                                                WWMK_Monitor *out);
/** @brief Direct center-point monitor lookup used behind queued actions. */
int wwmk_internal_window_monitor_by_center_direct(WWMK_Window window,
                                                  WWMK_Monitor *out);

#ifdef __cplusplus
}
#endif
