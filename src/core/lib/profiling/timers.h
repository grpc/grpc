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

#ifndef GRPC_CORE_LIB_PROFILING_TIMERS_H
#define GRPC_CORE_LIB_PROFILING_TIMERS_H

#ifdef __cplusplus
extern "C" {
#endif

void gpr_timers_global_init(void);
void gpr_timers_global_destroy(void);

void gpr_timer_add_mark(const char *tagstr, int important, const char *file,
                        int line);
void gpr_timer_begin(const char *tagstr, int important, const char *file,
                     int line);
void gpr_timer_end(const char *tagstr, int important, const char *file,
                   int line);

void gpr_timers_set_log_filename(const char *filename);

#if !(defined(GRPC_STAP_PROFILER) + defined(GRPC_BASIC_PROFILER))
/* No profiling. No-op all the things. */
#define GPR_TIMER_MARK(tag, important) \
  do {                                 \
  } while (0)

#define GPR_TIMER_BEGIN(tag, important) \
  do {                                  \
  } while (0)

#define GPR_TIMER_END(tag, important) \
  do {                                \
  } while (0)

#else /* at least one profiler requested... */
/* ... hopefully only one. */
#if defined(GRPC_STAP_PROFILER) && defined(GRPC_BASIC_PROFILER)
#error "GRPC_STAP_PROFILER and GRPC_BASIC_PROFILER are mutually exclusive."
#endif

/* Generic profiling interface. */
#define GPR_TIMER_MARK(tag, important) \
  gpr_timer_add_mark(tag, important, __FILE__, __LINE__);

#define GPR_TIMER_BEGIN(tag, important) \
  gpr_timer_begin(tag, important, __FILE__, __LINE__);

#define GPR_TIMER_END(tag, important) \
  gpr_timer_end(tag, important, __FILE__, __LINE__);

#ifdef GRPC_STAP_PROFILER
/* Empty placeholder for now. */
#endif /* GRPC_STAP_PROFILER */

#ifdef GRPC_BASIC_PROFILER
/* Empty placeholder for now. */
#endif /* GRPC_BASIC_PROFILER */

#endif /* at least one profiler requested. */

#ifdef __cplusplus
}

#if (defined(GRPC_STAP_PROFILER) + defined(GRPC_BASIC_PROFILER))
namespace grpc {
class ProfileScope {
 public:
  ProfileScope(const char *desc, bool important) : desc_(desc) {
    GPR_TIMER_BEGIN(desc_, important ? 1 : 0);
  }
  ~ProfileScope() { GPR_TIMER_END(desc_, 0); }

 private:
  const char *const desc_;
};
}

#define GPR_TIMER_SCOPE(tag, important) \
  ::grpc::ProfileScope _profile_scope_##__LINE__((tag), (important))
#else
#define GPR_TIMER_SCOPE(tag, important) \
  do {                                  \
  } while (false)
#endif
#endif

#endif /* GRPC_CORE_LIB_PROFILING_TIMERS_H */
