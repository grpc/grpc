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

/* Posix implementation for gpr threads. */

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SYNC

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct thd_arg {
  void (*body)(void* arg); /* body of a thread */
  void* arg;               /* argument to a thread */
  const char* name;        /* name of thread */
};

/* Body of every thread started via gpr_thd_new. */
static void* thread_body(void* v) {
  struct thd_arg a = *(struct thd_arg*)v;
  free(v);
  if (a.name != NULL) {
#if GPR_APPLE_PTHREAD_NAME
    /* Apple supports 64 characters, and will truncate if it's longer. */
    pthread_setname_np(a.name);
#elif GPR_LINUX_PTHREAD_NAME
    /* Linux supports 16 characters max, and will error if it's longer. */
    char buf[16];
    strncpy(buf, a.name, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    pthread_setname_np(pthread_self(), buf);
#endif // GPR_APPLE_PTHREAD_NAME
  }
  (*a.body)(a.arg);
  return nullptr;
}

int gpr_thd_new(gpr_thd_id* t, const char* thd_name,
                void (*thd_body)(void* arg), void* arg,
                const gpr_thd_options* options) {
  int thread_started;
  pthread_attr_t attr;
  pthread_t p;
  /* don't use gpr_malloc as we may cause an infinite recursion with
   * the profiling code */
  struct thd_arg* a = (struct thd_arg*)malloc(sizeof(*a));
  GPR_ASSERT(a != nullptr);
  a->body = thd_body;
  a->arg = arg;
  a->name = thd_name;
  
  GPR_ASSERT(pthread_attr_init(&attr) == 0);
  if (gpr_thd_options_is_detached(options)) {
    GPR_ASSERT(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) ==
               0);
  } else {
    GPR_ASSERT(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) ==
               0);
  }
  thread_started = (pthread_create(&p, &attr, &thread_body, a) == 0);
  GPR_ASSERT(pthread_attr_destroy(&attr) == 0);
  if (!thread_started) {
    /* don't use gpr_free, as this was allocated using malloc (see above) */
    free(a);
  }
  *t = (gpr_thd_id)p;
  return thread_started;
}

gpr_thd_id gpr_thd_currentid(void) { return (gpr_thd_id)pthread_self(); }

void gpr_thd_join(gpr_thd_id t) { pthread_join((pthread_t)t, nullptr); }

#endif /* GPR_POSIX_SYNC */
