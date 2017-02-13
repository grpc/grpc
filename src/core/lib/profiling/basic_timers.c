/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/support/port_platform.h>

#ifdef GRPC_BASIC_PROFILER

#include "src/core/lib/profiling/timers.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <stdio.h>
#include <string.h>

#include "src/core/lib/support/env.h"

typedef enum { BEGIN = '{', END = '}', MARK = '.' } marker_type;

typedef struct gpr_timer_entry {
  gpr_timespec tm;
  const char *tagstr;
  const char *file;
  short line;
  char type;
  uint8_t important;
  int thd;
} gpr_timer_entry;

#define MAX_COUNT 1000000

typedef struct gpr_timer_log {
  size_t num_entries;
  struct gpr_timer_log *next;
  struct gpr_timer_log *prev;
  gpr_timer_entry log[MAX_COUNT];
} gpr_timer_log;

typedef struct gpr_timer_log_list {
  gpr_timer_log *head;
  /* valid iff head!=NULL */
  gpr_timer_log *tail;
} gpr_timer_log_list;

static __thread gpr_timer_log *g_thread_log;
static gpr_once g_once_init = GPR_ONCE_INIT;
static FILE *output_file;
static const char *output_filename_or_null = NULL;
static pthread_mutex_t g_mu;
static pthread_cond_t g_cv;
static gpr_timer_log_list g_in_progress_logs;
static gpr_timer_log_list g_done_logs;
static int g_shutdown;
static gpr_thd_id g_writing_thread;
static __thread int g_thread_id;
static int g_next_thread_id;
static int g_writing_enabled = 1;

static const char *output_filename() {
  if (output_filename_or_null == NULL) {
    output_filename_or_null = gpr_getenv("LATENCY_TRACE");
    if (output_filename_or_null == NULL ||
        strlen(output_filename_or_null) == 0) {
      output_filename_or_null = "latency_trace.txt";
    }
  }
  return output_filename_or_null;
}

static int timer_log_push_back(gpr_timer_log_list *list, gpr_timer_log *log) {
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

static gpr_timer_log *timer_log_pop_front(gpr_timer_log_list *list) {
  gpr_timer_log *out = list->head;
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

static void timer_log_remove(gpr_timer_log_list *list, gpr_timer_log *log) {
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

static void write_log(gpr_timer_log *log) {
  size_t i;
  if (output_file == NULL) {
    output_file = fopen(output_filename(), "w");
  }
  for (i = 0; i < log->num_entries; i++) {
    gpr_timer_entry *entry = &(log->log[i]);
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

static void writing_thread(void *unused) {
  gpr_timer_log *log;
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
      return;
    }
  }
}

static void flush_logs(gpr_timer_log_list *list) {
  gpr_timer_log *log;
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
  gpr_thd_join(g_writing_thread);

  gpr_log(GPR_INFO, "flushing logs");

  pthread_mutex_lock(&g_mu);
  flush_logs(&g_done_logs);
  flush_logs(&g_in_progress_logs);
  pthread_mutex_unlock(&g_mu);

  if (output_file) {
    fclose(output_file);
  }
}

void gpr_timers_set_log_filename(const char *filename) {
  output_filename_or_null = filename;
}

static void init_output() {
  gpr_thd_options options = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&options);
  gpr_thd_new(&g_writing_thread, writing_thread, NULL, &options);
  atexit(finish_writing);
}

static void rotate_log() {
  /* Using malloc here, as this code could end up being called by gpr_malloc */
  gpr_timer_log *new = malloc(sizeof(*new));
  gpr_once_init(&g_once_init, init_output);
  new->num_entries = 0;
  pthread_mutex_lock(&g_mu);
  if (g_thread_log != NULL) {
    timer_log_remove(&g_in_progress_logs, g_thread_log);
    if (timer_log_push_back(&g_done_logs, g_thread_log)) {
      pthread_cond_signal(&g_cv);
    }
  } else {
    g_thread_id = g_next_thread_id++;
  }
  timer_log_push_back(&g_in_progress_logs, new);
  pthread_mutex_unlock(&g_mu);
  g_thread_log = new;
}

static void gpr_timers_log_add(const char *tagstr, marker_type type,
                               int important, const char *file, int line) {
  gpr_timer_entry *entry;

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
void gpr_timer_add_mark(const char *tagstr, int important, const char *file,
                        int line) {
  gpr_timers_log_add(tagstr, MARK, important, file, line);
}

void gpr_timer_begin(const char *tagstr, int important, const char *file,
                     int line) {
  gpr_timers_log_add(tagstr, BEGIN, important, file, line);
}

void gpr_timer_end(const char *tagstr, int important, const char *file,
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

void gpr_timers_set_log_filename(const char *filename) {}

void gpr_timer_set_enabled(int enabled) {}
#endif /* GRPC_BASIC_PROFILER */
