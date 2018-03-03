/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/gpr/arena.h"

#include <inttypes.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/test_config.h"

static void test_noop(void) { gpr_arena_destroy(gpr_arena_create(1)); }

static void test(const char* name, size_t init_size, const size_t* allocs,
                 size_t nallocs) {
  gpr_strvec v;
  char* s;
  gpr_strvec_init(&v);
  gpr_asprintf(&s, "test '%s': %" PRIdPTR " <- {", name, init_size);
  gpr_strvec_add(&v, s);
  for (size_t i = 0; i < nallocs; i++) {
    gpr_asprintf(&s, "%" PRIdPTR ",", allocs[i]);
    gpr_strvec_add(&v, s);
  }
  gpr_strvec_add(&v, gpr_strdup("}"));
  s = gpr_strvec_flatten(&v, nullptr);
  gpr_strvec_destroy(&v);
  gpr_log(GPR_INFO, "%s", s);
  gpr_free(s);

  gpr_arena* a = gpr_arena_create(init_size);
  void** ps = static_cast<void**>(gpr_zalloc(sizeof(*ps) * nallocs));
  for (size_t i = 0; i < nallocs; i++) {
    ps[i] = gpr_arena_alloc(a, allocs[i]);
    // ensure the returned address is aligned
    GPR_ASSERT(((intptr_t)ps[i] & 0xf) == 0);
    // ensure no duplicate results
    for (size_t j = 0; j < i; j++) {
      GPR_ASSERT(ps[i] != ps[j]);
    }
    // ensure writable
    memset(ps[i], 1, allocs[i]);
  }
  gpr_arena_destroy(a);
  gpr_free(ps);
}

#define TEST(name, init_size, ...)                     \
  static const size_t allocs_##name[] = {__VA_ARGS__}; \
  test(#name, init_size, allocs_##name, GPR_ARRAY_SIZE(allocs_##name))

#define CONCURRENT_TEST_THREADS 100

size_t concurrent_test_iterations() {
  if (sizeof(void*) < 8) return 1000;
  return 100000;
}

typedef struct {
  gpr_event ev_start;
  gpr_arena* arena;
} concurrent_test_args;

static void concurrent_test_body(void* arg) {
  concurrent_test_args* a = static_cast<concurrent_test_args*>(arg);
  gpr_event_wait(&a->ev_start, gpr_inf_future(GPR_CLOCK_REALTIME));
  for (size_t i = 0; i < concurrent_test_iterations(); i++) {
    *static_cast<char*>(gpr_arena_alloc(a->arena, 1)) = static_cast<char>(i);
  }
}

static void concurrent_test(void) {
  gpr_log(GPR_DEBUG, "concurrent_test");

  concurrent_test_args args;
  gpr_event_init(&args.ev_start);
  args.arena = gpr_arena_create(1024);

  grpc_core::Thread thds[CONCURRENT_TEST_THREADS];

  for (int i = 0; i < CONCURRENT_TEST_THREADS; i++) {
    thds[i] =
        grpc_core::Thread("grpc_concurrent_test", concurrent_test_body, &args);
    thds[i].Start();
  }

  gpr_event_set(&args.ev_start, (void*)1);

  for (auto& th : thds) {
    th.Join();
  }

  gpr_arena_destroy(args.arena);
}

int main(int argc, char* argv[]) {
  grpc_test_init(argc, argv);

  test_noop();
  TEST(0_1, 0, 1);
  TEST(1_1, 1, 1);
  TEST(1_2, 1, 2);
  TEST(1_3, 1, 3);
  TEST(1_inc, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
  TEST(6_123, 6, 1, 2, 3);
  concurrent_test();

  return 0;
}
