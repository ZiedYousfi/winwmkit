#include "winwmkit/winwmkit.h"

#include <stdlib.h>
#include <string.h>

#include <windows.h>

enum {
  WWMK_PIPE_STATUS_INVALID_ARGUMENT = -1,
  WWMK_PIPE_STATUS_OUT_OF_MEMORY = -2,
  WWMK_PIPE_STATUS_CREATE_FAILED = -3,
  WWMK_PIPE_STATUS_THREAD_FAILED = -4,
  WWMK_PIPE_STATUS_STOP_FAILED = -5
};

struct WWMK_PipeServer {
  HANDLE thread;
  HANDLE stop_event;
  WWMK_PipeMessageCallback callback;
  void *userdata;
  char full_name[256];
};

static int wwmk_pipe_build_full_name(const char *pipe_name, char *out,
                                     size_t out_size) {
  static const char prefix[] = "\\\\.\\pipe\\";
  size_t prefix_len = sizeof(prefix) - 1u;
  size_t pipe_len = 0;

  if (pipe_name == NULL || out == NULL || out_size == 0) {
    return WWMK_PIPE_STATUS_INVALID_ARGUMENT;
  }

  pipe_len = strlen(pipe_name);
  if (pipe_len == 0) {
    return WWMK_PIPE_STATUS_INVALID_ARGUMENT;
  }

  if (pipe_len >= prefix_len && strncmp(pipe_name, prefix, prefix_len) == 0) {
    if (pipe_len + 1u > out_size) {
      return WWMK_PIPE_STATUS_INVALID_ARGUMENT;
    }

    memcpy(out, pipe_name, pipe_len + 1u);
    return 0;
  }

  if (prefix_len + pipe_len + 1u > out_size) {
    return WWMK_PIPE_STATUS_INVALID_ARGUMENT;
  }

  memcpy(out, prefix, prefix_len);
  memcpy(out + prefix_len, pipe_name, pipe_len + 1u);
  return 0;
}

static int wwmk_pipe_buffer_reserve(char **buffer, size_t *capacity,
                                    size_t needed) {
  char *grown = NULL;
  size_t next_capacity = 0;

  if (buffer == NULL || capacity == NULL) {
    return WWMK_PIPE_STATUS_INVALID_ARGUMENT;
  }

  if (needed <= *capacity) {
    return 0;
  }

  next_capacity = *capacity > 0 ? *capacity : 1024u;
  while (next_capacity < needed) {
    if (next_capacity > ((size_t)-1) / 2u) {
      next_capacity = needed;
      break;
    }
    next_capacity *= 2u;
  }

  grown = (char *)realloc(*buffer, next_capacity);
  if (grown == NULL) {
    return WWMK_PIPE_STATUS_OUT_OF_MEMORY;
  }

  *buffer = grown;
  *capacity = next_capacity;
  return 0;
}

static int wwmk_pipe_append_chunk(char **message, size_t *message_size,
                                  size_t *message_capacity,
                                  const char *chunk, DWORD chunk_size) {
  size_t write_offset = 0;
  int status = 0;

  if (message == NULL || message_size == NULL || message_capacity == NULL) {
    return WWMK_PIPE_STATUS_INVALID_ARGUMENT;
  }

  write_offset = *message_size;
  status =
      wwmk_pipe_buffer_reserve(message, message_capacity,
                               write_offset + (size_t)chunk_size + 1u);
  if (status < 0) {
    return status;
  }

  if (chunk_size > 0) {
    memcpy(*message + write_offset, chunk, (size_t)chunk_size);
  }

  *message_size += (size_t)chunk_size;
  (*message)[*message_size] = '\0';
  return 0;
}

static void wwmk_pipe_dispatch_message(WWMK_PipeServer *server, char *message,
                                       size_t message_size) {
  if (server == NULL || server->callback == NULL) {
    return;
  }

  server->callback(message, message_size, server->userdata);
}

static int wwmk_pipe_read_messages(WWMK_PipeServer *server, HANDLE pipe) {
  char chunk[1024];
  char *message = NULL;
  size_t message_size = 0;
  size_t message_capacity = 0;
  int status = 0;

  if (server == NULL || pipe == INVALID_HANDLE_VALUE) {
    return WWMK_PIPE_STATUS_INVALID_ARGUMENT;
  }

  for (;;) {
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(pipe, chunk, sizeof(chunk), &bytes_read, NULL);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();

    if (WaitForSingleObject(server->stop_event, 0) == WAIT_OBJECT_0) {
      status = 0;
      break;
    }

    if (ok || error == ERROR_MORE_DATA) {
      status = wwmk_pipe_append_chunk(&message, &message_size, &message_capacity,
                                      chunk, bytes_read);
      if (status < 0) {
        break;
      }

      if (error == ERROR_MORE_DATA) {
        continue;
      }

      wwmk_pipe_dispatch_message(server, message, message_size);
      message_size = 0;
      continue;
    }

    if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA) {
      status = 0;
      break;
    }

    if (error == ERROR_OPERATION_ABORTED) {
      status = 0;
      break;
    }

    status = WWMK_PIPE_STATUS_CREATE_FAILED;
    break;
  }

  free(message);
  return status;
}

static DWORD WINAPI wwmk_pipe_server_thread_main(LPVOID arg) {
  WWMK_PipeServer *server = (WWMK_PipeServer *)arg;

  if (server == NULL) {
    return 1;
  }

  while (WaitForSingleObject(server->stop_event, 0) != WAIT_OBJECT_0) {
    HANDLE pipe = CreateNamedPipeA(
        server->full_name, PIPE_ACCESS_INBOUND,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, 1024, 1024, 0, NULL);
    BOOL connected = FALSE;
    DWORD error = ERROR_SUCCESS;

    if (pipe == INVALID_HANDLE_VALUE) {
      return 1;
    }

    connected = ConnectNamedPipe(pipe, NULL) ? TRUE
                                             : (GetLastError() ==
                                                ERROR_PIPE_CONNECTED);
    error = connected ? ERROR_SUCCESS : GetLastError();

    if (!connected) {
      if (error != ERROR_OPERATION_ABORTED &&
          WaitForSingleObject(server->stop_event, 0) != WAIT_OBJECT_0) {
        CloseHandle(pipe);
        return 1;
      }

      CloseHandle(pipe);
      break;
    }

    (void)wwmk_pipe_read_messages(server, pipe);
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
  }

  return 0;
}

WWMK_PipeServer *wwmk_pipe_server_start(const char *pipe_name,
                                        WWMK_PipeMessageCallback callback,
                                        void *userdata) {
  WWMK_PipeServer *server = NULL;
  int status = 0;

  if (callback == NULL) {
    return NULL;
  }

  server = (WWMK_PipeServer *)calloc(1, sizeof(*server));
  if (server == NULL) {
    return NULL;
  }

  status = wwmk_pipe_build_full_name(pipe_name, server->full_name,
                                     sizeof(server->full_name));
  if (status < 0) {
    free(server);
    return NULL;
  }

  server->callback = callback;
  server->userdata = userdata;
  server->stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (server->stop_event == NULL) {
    free(server);
    return NULL;
  }

  server->thread =
      CreateThread(NULL, 0, wwmk_pipe_server_thread_main, server, 0, NULL);
  if (server->thread == NULL) {
    CloseHandle(server->stop_event);
    free(server);
    return NULL;
  }

  return server;
}

int wwmk_pipe_server_stop(WWMK_PipeServer *server) {
  DWORD wait_result = WAIT_OBJECT_0;

  if (server == NULL) {
    return WWMK_PIPE_STATUS_INVALID_ARGUMENT;
  }

  if (server->stop_event != NULL) {
    (void)SetEvent(server->stop_event);
  }

  if (server->thread != NULL) {
    (void)CancelSynchronousIo(server->thread);
    wait_result = WaitForSingleObject(server->thread, INFINITE);
    CloseHandle(server->thread);
    server->thread = NULL;
  }

  if (server->stop_event != NULL) {
    CloseHandle(server->stop_event);
    server->stop_event = NULL;
  }

  free(server);

  if (wait_result != WAIT_OBJECT_0) {
    return WWMK_PIPE_STATUS_STOP_FAILED;
  }

  return 0;
}
