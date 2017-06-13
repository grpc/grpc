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

void gpr_timer_set_enabled(int enabled);

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
