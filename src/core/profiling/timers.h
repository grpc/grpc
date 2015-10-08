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

#ifndef GRPC_CORE_PROFILING_TIMERS_H
#define GRPC_CORE_PROFILING_TIMERS_H

#ifdef __cplusplus
extern "C" {
#endif

void grpc_timers_global_init(void);
void grpc_timers_global_destroy(void);

void grpc_timer_add_mark(int tag, const char *tagstr, int important,
                         const char *file, int line);
void grpc_timer_begin(int tag, const char *tagstr, int important, const char *file,
                      int line);
void grpc_timer_end(int tag, const char *tagstr, int important, const char *file,
                    int line);

enum grpc_profiling_tags {
  /* Any GRPC_PTAG_* >= than the threshold won't generate any profiling mark. */
  GRPC_PTAG_IGNORE_THRESHOLD = 1000000,

  /* Re. Protos. */
  GRPC_PTAG_PROTO_SERIALIZE = 100 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_PROTO_DESERIALIZE = 101 + GRPC_PTAG_IGNORE_THRESHOLD,

  /* Re. sockets. */
  GRPC_PTAG_HANDLE_READ = 200 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_SENDMSG = 201,
  GRPC_PTAG_RECVMSG = 202,
  GRPC_PTAG_POLL = 203,
  GRPC_PTAG_TCP_CB_WRITE = 204 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_TCP_WRITE = 205 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_BECOME_READABLE = 207,

  GRPC_PTAG_MUTEX_LOCK = 250,
  GRPC_PTAG_MUTEX_UNLOCK = 254,
  GRPC_PTAG_MALLOC = 251,
  GRPC_PTAG_REALLOC = 252,
  GRPC_PTAG_FREE = 253,

  /* C++ */
  GRPC_PTAG_CPP_CALL_CREATED = 300 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_CPP_PERFORM_OPS = 301 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_CLIENT_UNARY_CALL = 302,
  GRPC_PTAG_SERVER_CALL = 303,
  GRPC_PTAG_SERVER_CALLBACK = 304,

  /* Transports */
  GRPC_PTAG_HTTP2_RECV_DATA = 400,
  GRPC_PTAG_HTTP2_UNLOCK = 401,
  GRPC_PTAG_HTTP2_WRITING_ACTION = 402,
  GRPC_PTAG_HTTP2_TERMINATE_WRITING = 403,

  /* Completion queue */
  GRPC_PTAG_CQ_NEXT = 501,
  GRPC_PTAG_CQ_PLUCK = 502,
  GRPC_PTAG_POLLSET_WORK = 503,
  GRPC_PTAG_EXEC_CTX_FLUSH = 504,
  GRPC_PTAG_EXEC_CTX_STEP = 505,

  /* Surface */
  GRPC_PTAG_CALL_START_BATCH = 600,
  GRPC_PTAG_CALL_ON_DONE_RECV = 601,
  GRPC_PTAG_CALL_UNLOCK = 602,
  GRPC_PTAG_CALL_ON_DONE_SEND = 602,

  /* Channel */
  GRPC_PTAG_CHANNEL_PICKED_TARGET = 700,

  /* > 1024 Unassigned reserved. For any miscellaneous use.
   * Use addition to generate tags from this base or take advantage of the 10
   * zero'd bits for OR-ing. */
  GRPC_PTAG_OTHER_BASE = 1024
};

#if !(defined(GRPC_STAP_PROFILER) + defined(GRPC_BASIC_PROFILER))
/* No profiling. No-op all the things. */
#define GRPC_TIMER_MARK(tag, important) \
  do {                           \
  } while (0)

#define GRPC_TIMER_BEGIN(tag, important) \
  do {                            \
  } while (0)

#define GRPC_TIMER_END(tag, important) \
  do {                          \
  } while (0)

#else /* at least one profiler requested... */
/* ... hopefully only one. */
#if defined(GRPC_STAP_PROFILER) && defined(GRPC_BASIC_PROFILER)
#error "GRPC_STAP_PROFILER and GRPC_BASIC_PROFILER are mutually exclusive."
#endif

/* Generic profiling interface. */
#define GRPC_TIMER_MARK(tag, important)                                         \
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {                                \
    grpc_timer_add_mark(tag, #tag, important, __FILE__, \
                        __LINE__);                                       \
  }

#define GRPC_TIMER_BEGIN(tag, important)                                     \
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {                             \
    grpc_timer_begin(tag, #tag, important, __FILE__, \
                     __LINE__);                                       \
  }

#define GRPC_TIMER_END(tag, important)                                                \
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {                                      \
    grpc_timer_end(tag, #tag, important, __FILE__, __LINE__); \
  }

#ifdef GRPC_STAP_PROFILER
/* Empty placeholder for now. */
#endif /* GRPC_STAP_PROFILER */

#ifdef GRPC_BASIC_PROFILER
/* Empty placeholder for now. */
#endif /* GRPC_BASIC_PROFILER */

#endif /* at least one profiler requested. */

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_PROFILING_TIMERS_H */
