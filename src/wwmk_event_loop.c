#include "wwmk_event_loop.h"

#include <stdlib.h>

static void wwmk_internal_todo_abort(void) {
  abort();
}

static int wwmk_internal_todo_int(void) {
  wwmk_internal_todo_abort();
  return 0;
}

static DWORD wwmk_internal_todo_dword(void) {
  wwmk_internal_todo_abort();
  return 0;
}

int wwmk_event_queue_init(WWMK_EventQueue *queue, size_t capacity) {
  (void)queue;
  (void)capacity;
  return wwmk_internal_todo_int();
}

void wwmk_event_queue_destroy(WWMK_EventQueue *queue) {
  (void)queue;
  wwmk_internal_todo_abort();
}

int wwmk_event_queue_push(WWMK_EventQueue *queue,
                          const WWMK_InternalEvent *event) {
  (void)queue;
  (void)event;
  return wwmk_internal_todo_int();
}

int wwmk_event_queue_pop_wait(WWMK_EventQueue *queue, WWMK_InternalEvent *out) {
  (void)queue;
  (void)out;
  return wwmk_internal_todo_int();
}

int wwmk_event_queue_pop_nowait(WWMK_EventQueue *queue,
                                WWMK_InternalEvent *out) {
  (void)queue;
  (void)out;
  return wwmk_internal_todo_int();
}

void wwmk_event_queue_close(WWMK_EventQueue *queue) {
  (void)queue;
  wwmk_internal_todo_abort();
}

int wwmk_event_loop_init(WWMK_EventLoop *loop, size_t queue_capacity,
                         WWMK_InternalEventCallback callback, void *userdata) {
  (void)loop;
  (void)queue_capacity;
  (void)callback;
  (void)userdata;
  return wwmk_internal_todo_int();
}

void wwmk_event_loop_destroy(WWMK_EventLoop *loop) {
  (void)loop;
  wwmk_internal_todo_abort();
}

int wwmk_event_loop_start(WWMK_EventLoop *loop) {
  (void)loop;
  return wwmk_internal_todo_int();
}

int wwmk_event_loop_stop(WWMK_EventLoop *loop) {
  (void)loop;
  return wwmk_internal_todo_int();
}

DWORD WINAPI wwmk_event_loop_thread_main(LPVOID arg) {
  (void)arg;
  return wwmk_internal_todo_dword();
}

int wwmk_event_loop_translate_event(const WWMK_InternalEvent *internal_event,
                                    WWMK_Event *public_event) {
  (void)internal_event;
  (void)public_event;
  return wwmk_internal_todo_int();
}

void wwmk_event_loop_dispatch(WWMK_EventLoop *loop,
                              const WWMK_InternalEvent *event) {
  (void)loop;
  (void)event;
  wwmk_internal_todo_abort();
}
