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
#include "src/core/profiling/timers_preciseclock.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <stdio.h>

typedef enum {
  BEGIN = '{',
  END = '}',
  MARK = '.'
} marker_type;

typedef struct grpc_timer_entry {
  grpc_precise_clock tm;
  gpr_thd_id thd;
  int tag;
  marker_type type;
  void* id;
  const char* file;
  int line;
} grpc_timer_entry;

struct grpc_timers_log {
  gpr_mu mu;
  grpc_timer_entry* log;
  int num_entries;
  int capacity;
  int capacity_limit;
  FILE* fp;
};

grpc_timers_log* grpc_timers_log_global = NULL;

static grpc_timers_log* grpc_timers_log_create(int capacity_limit, FILE* dump) {
  grpc_timers_log* log = gpr_malloc(sizeof(*log));

  /* TODO (vpai): Allow allocation below limit */
  log->log = gpr_malloc(capacity_limit * sizeof(*log->log));

  /* TODO (vpai): Improve concurrency, do per-thread logging? */
  gpr_mu_init(&log->mu);

  log->num_entries = 0;
  log->capacity = log->capacity_limit = capacity_limit;

  log->fp = dump;

  return log;
}

static void log_report_locked(grpc_timers_log* log) {
  FILE* fp = log->fp;
  int i;
  for (i = 0; i < log->num_entries; i++) {
    grpc_timer_entry* entry = &(log->log[i]);
    fprintf(fp, "GRPC_LAT_PROF ");
    grpc_precise_clock_print(&entry->tm, fp);
    fprintf(fp, " %p %c %d %p %s %d\n", (void*)(gpr_intptr)entry->thd, entry->type, entry->tag,
            entry->id, entry->file, entry->line);
  }

  /* Now clear out the log */
  log->num_entries = 0;
}

static void grpc_timers_log_destroy(grpc_timers_log* log) {
  gpr_mu_lock(&log->mu);
  log_report_locked(log);
  gpr_mu_unlock(&log->mu);

  gpr_free(log->log);
  gpr_mu_destroy(&log->mu);

  gpr_free(log);
}

static void grpc_timers_log_add(grpc_timers_log* log, int tag, marker_type type, void* id,
                                const char* file, int line) {
  grpc_timer_entry* entry;

  /* TODO (vpai) : Improve concurrency */
  gpr_mu_lock(&log->mu);
  if (log->num_entries == log->capacity_limit) {
    log_report_locked(log);
  }

  entry = &log->log[log->num_entries++];

  grpc_precise_clock_now(&entry->tm);
  entry->tag = tag;
  entry->type = type;
  entry->id = id;
  entry->file = file;
  entry->line = line;
  entry->thd = gpr_thd_currentid();

  gpr_mu_unlock(&log->mu);
}

/* Latency profiler API implementation. */
void grpc_timer_add_mark(int tag, void* id, const char* file, int line) {
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {
    grpc_timers_log_add(grpc_timers_log_global, tag, MARK, id, file, line);
  }
}

void grpc_timer_begin(int tag, void* id, const char *file, int line) {
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {
    grpc_timers_log_add(grpc_timers_log_global, tag, BEGIN, id, file, line);
  }
}

void grpc_timer_end(int tag, void* id, const char *file, int line) {
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {
    grpc_timers_log_add(grpc_timers_log_global, tag, END, id, file, line);
  }
}

/* Basic profiler specific API functions. */
void grpc_timers_global_init(void) {
  grpc_timers_log_global = grpc_timers_log_create(100000, stdout);
}

void grpc_timers_global_destroy(void) {
  grpc_timers_log_destroy(grpc_timers_log_global);
}

#else  /* !GRPC_BASIC_PROFILER */
void grpc_timers_global_init(void) {}
void grpc_timers_global_destroy(void) {}
#endif /* GRPC_BASIC_PROFILER */
