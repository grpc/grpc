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
  MARK = '.',
  IMPORTANT = '!'
} marker_type;

typedef struct grpc_timer_entry {
  grpc_precise_clock tm;
  int tag;
  const char* tagstr;
  marker_type type;
  void* id;
  const char* file;
  int line;
} grpc_timer_entry;

#define MAX_COUNT (1024 * 1024 / sizeof(grpc_timer_entry))

static __thread grpc_timer_entry log[MAX_COUNT];
static __thread int count;

static void log_report() {
  int i;
  for (i = 0; i < count; i++) {
    grpc_timer_entry* entry = &(log[i]);
    printf("GRPC_LAT_PROF " GRPC_PRECISE_CLOCK_FORMAT
           " %p %c %d(%s) %p %s %d\n",
           GRPC_PRECISE_CLOCK_PRINTF_ARGS(&entry->tm),
           (void*)(gpr_intptr)gpr_thd_currentid(), entry->type, entry->tag,
           entry->tagstr, entry->id, entry->file, entry->line);
  }

  /* Now clear out the log */
  count = 0;
}

static void grpc_timers_log_add(int tag, const char* tagstr, marker_type type,
                                void* id, const char* file, int line) {
  grpc_timer_entry* entry;

  /* TODO (vpai) : Improve concurrency */
  if (count == MAX_COUNT) {
    log_report();
  }

  entry = &log[count++];

  grpc_precise_clock_now(&entry->tm);
  entry->tag = tag;
  entry->tagstr = tagstr;
  entry->type = type;
  entry->id = id;
  entry->file = file;
  entry->line = line;
}

/* Latency profiler API implementation. */
void grpc_timer_add_mark(int tag, const char* tagstr, void* id,
                         const char* file, int line) {
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {
    grpc_timers_log_add(tag, tagstr, MARK, id, file, line);
  }
}

void grpc_timer_add_important_mark(int tag, const char* tagstr, void* id,
                                   const char* file, int line) {
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {
    grpc_timers_log_add(tag, tagstr, IMPORTANT, id, file, line);
  }
}

void grpc_timer_begin(int tag, const char* tagstr, void* id, const char* file,
                      int line) {
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {
    grpc_timers_log_add(tag, tagstr, BEGIN, id, file, line);
  }
}

void grpc_timer_end(int tag, const char* tagstr, void* id, const char* file,
                    int line) {
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {
    grpc_timers_log_add(tag, tagstr, END, id, file, line);
  }
}

/* Basic profiler specific API functions. */
void grpc_timers_global_init(void) {}

void grpc_timers_global_destroy(void) {}

#else  /* !GRPC_BASIC_PROFILER */
void grpc_timers_global_init(void) {}
void grpc_timers_global_destroy(void) {}
#endif /* GRPC_BASIC_PROFILER */
