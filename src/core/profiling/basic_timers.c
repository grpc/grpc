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

#include "src/core/profiling/timers.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <stdio.h>

typedef enum { BEGIN = '{', END = '}', MARK = '.' } marker_type;

typedef struct grpc_timer_entry {
  gpr_timespec tm;
  const char *tagstr;
  const char *file;
  int line;
  char type;
  gpr_uint8 important;
} grpc_timer_entry;

#define MAX_COUNT (1024 * 1024 / sizeof(grpc_timer_entry))

static __thread grpc_timer_entry g_log[MAX_COUNT];
static __thread int g_count;
static gpr_once g_once_init = GPR_ONCE_INIT;
static FILE *output_file;

static void close_output() { fclose(output_file); }

static void init_output() {
  output_file = fopen("latency_trace.txt", "w");
  GPR_ASSERT(output_file);
  atexit(close_output);
}

static void log_report() {
  int i;
  gpr_once_init(&g_once_init, init_output);
  for (i = 0; i < g_count; i++) {
    grpc_timer_entry *entry = &(g_log[i]);
    fprintf(output_file,
            "{\"t\": %ld.%09d, \"thd\": \"%p\", \"type\": \"%c\", \"tag\": "
            "\"%s\", \"file\": \"%s\", \"line\": %d, \"imp\": %d}\n",
            entry->tm.tv_sec, entry->tm.tv_nsec,
            (void *)(gpr_intptr)gpr_thd_currentid(), entry->type, entry->tagstr,
            entry->file, entry->line, entry->important);
  }

  /* Now clear out the log */
  g_count = 0;
}

static void grpc_timers_log_add(const char *tagstr, marker_type type,
                                int important, const char *file, int line) {
  grpc_timer_entry *entry;

  /* TODO (vpai) : Improve concurrency */
  if (g_count == MAX_COUNT) {
    log_report();
  }

  entry = &g_log[g_count++];

  entry->tm = gpr_now(GPR_CLOCK_PRECISE);
  entry->tagstr = tagstr;
  entry->type = type;
  entry->file = file;
  entry->line = line;
  entry->important = important != 0;
}

/* Latency profiler API implementation. */
void grpc_timer_add_mark(const char *tagstr, int important, const char *file,
                         int line) {
  grpc_timers_log_add(tagstr, MARK, important, file, line);
}

void grpc_timer_begin(const char *tagstr, int important, const char *file,
                      int line) {
  grpc_timers_log_add(tagstr, BEGIN, important, file, line);
}

void grpc_timer_end(const char *tagstr, int important, const char *file,
                    int line) {
  grpc_timers_log_add(tagstr, END, important, file, line);
}

/* Basic profiler specific API functions. */
void grpc_timers_global_init(void) {}

void grpc_timers_global_destroy(void) {}

#else  /* !GRPC_BASIC_PROFILER */
void grpc_timers_global_init(void) {}

void grpc_timers_global_destroy(void) {}
#endif /* GRPC_BASIC_PROFILER */
