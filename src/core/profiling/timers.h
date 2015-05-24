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

void grpc_timer_add_mark(int tag, const char *tagstr, void *id,
                         const char *file, int line);
void grpc_timer_add_important_mark(int tag, const char *tagstr, void *id,
                                   const char *file, int line);
void grpc_timer_begin(int tag, const char *tagstr, void *id, const char *file,
                      int line);
void grpc_timer_end(int tag, const char *tagstr, void *id, const char *file,
                    int line);

enum grpc_profiling_tags {
  /* Any GRPC_PTAG_* >= than the threshold won't generate any profiling mark. */
  GRPC_PTAG_IGNORE_THRESHOLD = 1000000,

  /* Re. Protos. */
  GRPC_PTAG_PROTO_SERIALIZE = 100 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_PROTO_DESERIALIZE = 101 + GRPC_PTAG_IGNORE_THRESHOLD,

  /* Re. sockets. */
  GRPC_PTAG_HANDLE_READ = 200 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_SENDMSG = 201 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_RECVMSG = 202 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_POLL_FINISHED = 203 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_TCP_CB_WRITE = 204 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_TCP_WRITE = 205 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_CALL_ON_DONE_RECV = 206 + GRPC_PTAG_IGNORE_THRESHOLD,

  /* C++ */
  GRPC_PTAG_CPP_CALL_CREATED = 300 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_CPP_PERFORM_OPS = 301 + GRPC_PTAG_IGNORE_THRESHOLD,

  /* Transports */
  GRPC_PTAG_HTTP2_UNLOCK = 401 + GRPC_PTAG_IGNORE_THRESHOLD,
  GRPC_PTAG_HTTP2_UNLOCK_CLEANUP = 402 + GRPC_PTAG_IGNORE_THRESHOLD,

  /* > 1024 Unassigned reserved. For any miscellaneous use.
  * Use addition to generate tags from this base or take advantage of the 10
  * zero'd bits for OR-ing. */
  GRPC_PTAG_OTHER_BASE = 1024
};

#if !(defined(GRPC_STAP_PROFILER) + defined(GRPC_BASIC_PROFILER))
/* No profiling. No-op all the things. */
#define GRPC_TIMER_MARK(tag, id) \
  do {                           \
  } while (0)

#define GRPC_TIMER_IMPORTANT_MARK(tag, id) \
  do {                           \
  } while (0)

#define GRPC_TIMER_BEGIN(tag, id) \
  do {                            \
  } while (0)

#define GRPC_TIMER_END(tag, id) \
  do {                          \
  } while (0)

#else /* at least one profiler requested... */
/* ... hopefully only one. */
#if defined(GRPC_STAP_PROFILER) && defined(GRPC_BASIC_PROFILER)
#error "GRPC_STAP_PROFILER and GRPC_BASIC_PROFILER are mutually exclusive."
#endif

/* Generic profiling interface. */
#define GRPC_TIMER_MARK(tag, id)                                         \
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {                                \
    grpc_timer_add_mark(tag, #tag, ((void *)(gpr_intptr)(id)), __FILE__, \
                        __LINE__);                                       \
  }

#define GRPC_TIMER_IMPORTANT_MARK(tag, id)                               \
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {                                \
    grpc_timer_add_important_mark(tag, #tag, ((void *)(gpr_intptr)(id)), \
                                  __FILE__, __LINE__);                   \
  }

#define GRPC_TIMER_BEGIN(tag, id)                                     \
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {                             \
    grpc_timer_begin(tag, #tag, ((void *)(gpr_intptr)(id)), __FILE__, \
                     __LINE__);                                       \
  }

#define GRPC_TIMER_END(tag, id)                                                \
  if (tag < GRPC_PTAG_IGNORE_THRESHOLD) {                                      \
    grpc_timer_end(tag, #tag, ((void *)(gpr_intptr)(id)), __FILE__, __LINE__); \
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
