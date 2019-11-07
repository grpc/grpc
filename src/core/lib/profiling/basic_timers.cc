/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/profiling/timers.h"

#ifdef GRPC_BASIC_PROFILER

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "src/core/lib/gprpp/global_config.h"
#include "src/core/lib/profiling/timers.h"

typedef enum { BEGIN = '{', END = '}', MARK = '.' } marker_type;

typedef struct gpr_timer_entry {
  gpr_timespec tm;
  const char* tagstr;
  const char* file;
  short line;
  char type;
  uint8_t important;
  int thd;
} gpr_timer_entry;

#define MAX_COUNT 1000000

typedef struct gpr_timer_log {
  size_t num_entries;
  struct gpr_timer_log* next;
  struct gpr_timer_log* prev;
  gpr_timer_entry log[MAX_COUNT];
} gpr_timer_log;

typedef struct gpr_timer_log_list {
  gpr_timer_log* head;
  /* valid iff head!=NULL */
  gpr_timer_log* tail;
} gpr_timer_log_list;

static __thread gpr_timer_log* g_thread_log;
static gpr_once g_once_init = GPR_ONCE_INIT;
static FILE* output_file;
static const char* output_filename_or_null = NULL;
static pthread_mutex_t g_mu;
static pthread_cond_t g_cv;
static gpr_timer_log_list g_in_progress_logs;
static gpr_timer_log_list g_done_logs;
static int g_shutdown;
static pthread_t g_writing_thread;
static __thread int g_thread_id;
static int g_next_thread_id;
static int g_writing_enabled = 1;

GPR_GLOBAL_CONFIG_DEFINE_STRING(grpc_latency_trace, "latency_trace.txt",
                                "Output file name for latency trace")

static const char* output_filename() {
  if (output_filename_or_null == NULL) {
    grpc_core::UniquePtr<char> value =
        GPR_GLOBAL_CONFIG_GET(grpc_latency_trace);
    if (strlen(value.get()) > 0) {
      output_filename_or_null = value.release();
    } else {
      output_filename_or_null = "latency_trace.txt";
    }
  }
  return output_filename_or_null;
}

static int timer_log_push_back(gpr_timer_log_list* list, gpr_timer_log* log) {
  if (list->head == NULL) {
    list->head = list->tail = log;
    log->next = log->prev = NULL;
    return 1;
  } else {
    log->prev = list->tail;
    log->next = NULL;
    list->tail->next = log;
    list->tail = log;
    return 0;
  }
}

static gpr_timer_log* timer_log_pop_front(gpr_timer_log_list* list) {
  gpr_timer_log* out = list->head;
  if (out != NULL) {
    list->head = out->next;
    if (list->head != NULL) {
      list->head->prev = NULL;
    } else {
      list->tail = NULL;
    }
  }
  return out;
}

static void timer_log_remove(gpr_timer_log_list* list, gpr_timer_log* log) {
  if (log->prev == NULL) {
    list->head = log->next;
    if (list->head != NULL) {
      list->head->prev = NULL;
    }
  } else {
    log->prev->next = log->next;
  }
  if (log->next == NULL) {
    list->tail = log->prev;
    if (list->tail != NULL) {
      list->tail->next = NULL;
    }
  } else {
    log->next->prev = log->prev;
  }
}

static void write_log(gpr_timer_log* log) {
  size_t i;
  if (output_file == NULL) {
    output_file = fopen(output_filename(), "w");
  }
  for (i = 0; i < log->num_entries; i++) {
    gpr_timer_entry* entry = &(log->log[i]);
    if (gpr_time_cmp(entry->tm, gpr_time_0(entry->tm.clock_type)) < 0) {
      entry->tm = gpr_time_0(entry->tm.clock_type);
    }
    fprintf(output_file,
            "{\"t\": %" PRId64
            ".%09d, \"thd\": \"%d\", \"type\": \"%c\", \"tag\": "
            "\"%s\", \"file\": \"%s\", \"line\": %d, \"imp\": %d}\n",
            entry->tm.tv_sec, entry->tm.tv_nsec, entry->thd, entry->type,
            entry->tagstr, entry->file, entry->line, entry->important);
  }
}

static void* writing_thread(void* unused) {
  gpr_timer_log* log;
  pthread_mutex_lock(&g_mu);
  for (;;) {
    while ((log = timer_log_pop_front(&g_done_logs)) == NULL && !g_shutdown) {
      pthread_cond_wait(&g_cv, &g_mu);
    }
    if (log != NULL) {
      pthread_mutex_unlock(&g_mu);
      write_log(log);
      free(log);
      pthread_mutex_lock(&g_mu);
    }
    if (g_shutdown) {
      pthread_mutex_unlock(&g_mu);
      return NULL;
    }
  }
}

static void flush_logs(gpr_timer_log_list* list) {
  gpr_timer_log* log;
  while ((log = timer_log_pop_front(list)) != NULL) {
    write_log(log);
    free(log);
  }
}

static void finish_writing(void) {
  pthread_mutex_lock(&g_mu);
  g_shutdown = 1;
  pthread_cond_signal(&g_cv);
  pthread_mutex_unlock(&g_mu);
  pthread_join(g_writing_thread, NULL);

  gpr_log(GPR_INFO, "flushing logs");

  pthread_mutex_lock(&g_mu);
  flush_logs(&g_done_logs);
  flush_logs(&g_in_progress_logs);
  pthread_mutex_unlock(&g_mu);

  if (output_file) {
    fclose(output_file);
  }
}

void gpr_timers_set_log_filename(const char* filename) {
  output_filename_or_null = filename;
}

static void init_output() {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  pthread_create(&g_writing_thread, &attr, &writing_thread, NULL);
  pthread_attr_destroy(&attr);

  atexit(finish_writing);
}

static void rotate_log() {
  /* Using malloc here, as this code could end up being called by gpr_malloc */
  gpr_timer_log* log = static_cast<gpr_timer_log*>(malloc(sizeof(*log)));
  gpr_once_init(&g_once_init, init_output);
  log->num_entries = 0;
  pthread_mutex_lock(&g_mu);
  if (g_thread_log != NULL) {
    timer_log_remove(&g_in_progress_logs, g_thread_log);
    if (timer_log_push_back(&g_done_logs, g_thread_log)) {
      pthread_cond_signal(&g_cv);
    }
  } else {
    g_thread_id = g_next_thread_id++;
  }
  timer_log_push_back(&g_in_progress_logs, log);
  pthread_mutex_unlock(&g_mu);
  g_thread_log = log;
}

static void gpr_timers_log_add(const char* tagstr, marker_type type,
                               int important, const char* file, int line) {
  gpr_timer_entry* entry;

  if (!g_writing_enabled) {
    return;
  }

  if (g_thread_log == NULL || g_thread_log->num_entries == MAX_COUNT) {
    rotate_log();
  }

  entry = &g_thread_log->log[g_thread_log->num_entries++];

  entry->tm = gpr_now(GPR_CLOCK_PRECISE);
  entry->tagstr = tagstr;
  entry->type = type;
  entry->file = file;
  entry->line = (short)line;
  entry->important = important != 0;
  entry->thd = g_thread_id;
}

/* Latency profiler API implementation. */
void gpr_timer_add_mark(const char* tagstr, int important, const char* file,
                        int line) {
  gpr_timers_log_add(tagstr, MARK, important, file, line);
}

void gpr_timer_begin(const char* tagstr, int important, const char* file,
                     int line) {
  gpr_timers_log_add(tagstr, BEGIN, important, file, line);
}

void gpr_timer_end(const char* tagstr, int important, const char* file,
                   int line) {
  gpr_timers_log_add(tagstr, END, important, file, line);
}

void gpr_timer_set_enabled(int enabled) { g_writing_enabled = enabled; }

/* Basic profiler specific API functions. */
void gpr_timers_global_init(void) {}

void gpr_timers_global_destroy(void) {}

#else  /* !GRPC_BASIC_PROFILER */
void gpr_timers_global_init(void) {}

void gpr_timers_global_destroy(void) {}

void gpr_timers_set_log_filename(const char* /*filename*/) {}

void gpr_timer_set_enabled(int /*enabled*/) {}
#endif /* GRPC_BASIC_PROFILER */
