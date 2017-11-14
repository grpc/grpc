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

/* Test of gpr time support. */

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test/core/util/test_config.h"

static void to_fp(void* arg, const char* buf, size_t len) {
  fwrite(buf, 1, len, (FILE*)arg);
}

/* Convert gpr_intmax x to ascii base b (2..16), and write with
   (*writer)(arg, ...), zero padding to "chars" digits).  */
static void i_to_s(intmax_t x, int base, int chars,
                   void (*writer)(void* arg, const char* buf, size_t len),
                   void* arg) {
  char buf[64];
  char fmt[32];
  GPR_ASSERT(base == 16 || base == 10);
  sprintf(fmt, "%%0%d%s", chars, base == 16 ? PRIxMAX : PRIdMAX);
  sprintf(buf, fmt, x);
  (*writer)(arg, buf, strlen(buf));
}

/* Convert ts to ascii, and write with (*writer)(arg, ...).  */
static void ts_to_s(gpr_timespec t,
                    void (*writer)(void* arg, const char* buf, size_t len),
                    void* arg) {
  if (t.tv_sec < 0 && t.tv_nsec != 0) {
    t.tv_sec++;
    t.tv_nsec = GPR_NS_PER_SEC - t.tv_nsec;
  }
  i_to_s(t.tv_sec, 10, 0, writer, arg);
  (*writer)(arg, ".", 1);
  i_to_s(t.tv_nsec, 10, 9, writer, arg);
}

static void test_values(void) {
  int i;

  gpr_timespec x = gpr_time_0(GPR_CLOCK_REALTIME);
  GPR_ASSERT(x.tv_sec == 0 && x.tv_nsec == 0);

  x = gpr_inf_future(GPR_CLOCK_REALTIME);
  fprintf(stderr, "far future ");
  i_to_s(x.tv_sec, 16, 16, &to_fp, stderr);
  fprintf(stderr, "\n");
  GPR_ASSERT(x.tv_sec == INT64_MAX);
  fprintf(stderr, "far future ");
  ts_to_s(x, &to_fp, stderr);
  fprintf(stderr, "\n");

  x = gpr_inf_past(GPR_CLOCK_REALTIME);
  fprintf(stderr, "far past   ");
  i_to_s(x.tv_sec, 16, 16, &to_fp, stderr);
  fprintf(stderr, "\n");
  GPR_ASSERT(x.tv_sec == INT64_MIN);
  fprintf(stderr, "far past   ");
  ts_to_s(x, &to_fp, stderr);
  fprintf(stderr, "\n");

  for (i = 1; i != 1000 * 1000 * 1000; i *= 10) {
    x = gpr_time_from_micros(i, GPR_TIMESPAN);
    GPR_ASSERT(x.tv_sec == i / GPR_US_PER_SEC &&
               x.tv_nsec == (i % GPR_US_PER_SEC) * GPR_NS_PER_US);
    x = gpr_time_from_nanos(i, GPR_TIMESPAN);
    GPR_ASSERT(x.tv_sec == i / GPR_NS_PER_SEC &&
               x.tv_nsec == (i % GPR_NS_PER_SEC));
    x = gpr_time_from_millis(i, GPR_TIMESPAN);
    GPR_ASSERT(x.tv_sec == i / GPR_MS_PER_SEC &&
               x.tv_nsec == (i % GPR_MS_PER_SEC) * GPR_NS_PER_MS);
  }

  /* Test possible overflow in conversion of -ve values. */
  x = gpr_time_from_micros(-(INT64_MAX - 999997), GPR_TIMESPAN);
  GPR_ASSERT(x.tv_sec < 0);
  GPR_ASSERT(x.tv_nsec >= 0 && x.tv_nsec < GPR_NS_PER_SEC);

  x = gpr_time_from_nanos(-(INT64_MAX - 999999997), GPR_TIMESPAN);
  GPR_ASSERT(x.tv_sec < 0);
  GPR_ASSERT(x.tv_nsec >= 0 && x.tv_nsec < GPR_NS_PER_SEC);

  x = gpr_time_from_millis(-(INT64_MAX - 997), GPR_TIMESPAN);
  GPR_ASSERT(x.tv_sec < 0);
  GPR_ASSERT(x.tv_nsec >= 0 && x.tv_nsec < GPR_NS_PER_SEC);

  /* Test general -ve values. */
  for (i = -1; i > -1000 * 1000 * 1000; i *= 7) {
    x = gpr_time_from_micros(i, GPR_TIMESPAN);
    GPR_ASSERT(x.tv_sec * GPR_US_PER_SEC + x.tv_nsec / GPR_NS_PER_US == i);
    x = gpr_time_from_nanos(i, GPR_TIMESPAN);
    GPR_ASSERT(x.tv_sec * GPR_NS_PER_SEC + x.tv_nsec == i);
    x = gpr_time_from_millis(i, GPR_TIMESPAN);
    GPR_ASSERT(x.tv_sec * GPR_MS_PER_SEC + x.tv_nsec / GPR_NS_PER_MS == i);
  }
}

static void test_add_sub(void) {
  int i;
  int j;
  int k;
  /* Basic addition and subtraction. */
  for (i = -100; i <= 100; i++) {
    for (j = -100; j <= 100; j++) {
      for (k = 1; k <= 10000000; k *= 10) {
        int sum = i + j;
        int diff = i - j;
        gpr_timespec it = gpr_time_from_micros(i * k, GPR_TIMESPAN);
        gpr_timespec jt = gpr_time_from_micros(j * k, GPR_TIMESPAN);
        gpr_timespec sumt = gpr_time_add(it, jt);
        gpr_timespec difft = gpr_time_sub(it, jt);
        if (gpr_time_cmp(gpr_time_from_micros(sum * k, GPR_TIMESPAN), sumt) !=
            0) {
          fprintf(stderr, "i %d  j %d  sum %d    sumt ", i, j, sum);
          ts_to_s(sumt, &to_fp, stderr);
          fprintf(stderr, "\n");
          GPR_ASSERT(0);
        }
        if (gpr_time_cmp(gpr_time_from_micros(diff * k, GPR_TIMESPAN), difft) !=
            0) {
          fprintf(stderr, "i %d  j %d  diff %d    diff ", i, j, diff);
          ts_to_s(sumt, &to_fp, stderr);
          fprintf(stderr, "\n");
          GPR_ASSERT(0);
        }
      }
    }
  }
}

static void test_overflow(void) {
  /* overflow */
  gpr_timespec x = gpr_time_from_micros(1, GPR_TIMESPAN);
  do {
    x = gpr_time_add(x, x);
  } while (gpr_time_cmp(x, gpr_inf_future(GPR_TIMESPAN)) < 0);
  GPR_ASSERT(gpr_time_cmp(x, gpr_inf_future(GPR_TIMESPAN)) == 0);
  x = gpr_time_from_micros(-1, GPR_TIMESPAN);
  do {
    x = gpr_time_add(x, x);
  } while (gpr_time_cmp(x, gpr_inf_past(GPR_TIMESPAN)) > 0);
  GPR_ASSERT(gpr_time_cmp(x, gpr_inf_past(GPR_TIMESPAN)) == 0);
}

static void test_sticky_infinities(void) {
  int i;
  int j;
  int k;
  gpr_timespec infinity[2];
  gpr_timespec addend[3];
  infinity[0] = gpr_inf_future(GPR_TIMESPAN);
  infinity[1] = gpr_inf_past(GPR_TIMESPAN);
  addend[0] = gpr_inf_future(GPR_TIMESPAN);
  addend[1] = gpr_inf_past(GPR_TIMESPAN);
  addend[2] = gpr_time_0(GPR_TIMESPAN);

  /* Infinities are sticky */
  for (i = 0; i != sizeof(infinity) / sizeof(infinity[0]); i++) {
    for (j = 0; j != sizeof(addend) / sizeof(addend[0]); j++) {
      gpr_timespec x = gpr_time_add(infinity[i], addend[j]);
      GPR_ASSERT(gpr_time_cmp(x, infinity[i]) == 0);
      x = gpr_time_sub(infinity[i], addend[j]);
      GPR_ASSERT(gpr_time_cmp(x, infinity[i]) == 0);
    }
    for (k = -200; k <= 200; k++) {
      gpr_timespec y = gpr_time_from_micros(k * 100000, GPR_TIMESPAN);
      gpr_timespec x = gpr_time_add(infinity[i], y);
      GPR_ASSERT(gpr_time_cmp(x, infinity[i]) == 0);
      x = gpr_time_sub(infinity[i], y);
      GPR_ASSERT(gpr_time_cmp(x, infinity[i]) == 0);
    }
  }
}

static void test_similar(void) {
  GPR_ASSERT(1 == gpr_time_similar(gpr_inf_future(GPR_TIMESPAN),
                                   gpr_inf_future(GPR_TIMESPAN),
                                   gpr_time_0(GPR_TIMESPAN)));
  GPR_ASSERT(1 == gpr_time_similar(gpr_inf_past(GPR_TIMESPAN),
                                   gpr_inf_past(GPR_TIMESPAN),
                                   gpr_time_0(GPR_TIMESPAN)));
  GPR_ASSERT(0 == gpr_time_similar(gpr_inf_past(GPR_TIMESPAN),
                                   gpr_inf_future(GPR_TIMESPAN),
                                   gpr_time_0(GPR_TIMESPAN)));
  GPR_ASSERT(0 == gpr_time_similar(gpr_inf_future(GPR_TIMESPAN),
                                   gpr_inf_past(GPR_TIMESPAN),
                                   gpr_time_0(GPR_TIMESPAN)));
  GPR_ASSERT(1 == gpr_time_similar(gpr_time_from_micros(10, GPR_TIMESPAN),
                                   gpr_time_from_micros(10, GPR_TIMESPAN),
                                   gpr_time_0(GPR_TIMESPAN)));
  GPR_ASSERT(1 == gpr_time_similar(gpr_time_from_micros(10, GPR_TIMESPAN),
                                   gpr_time_from_micros(15, GPR_TIMESPAN),
                                   gpr_time_from_micros(10, GPR_TIMESPAN)));
  GPR_ASSERT(1 == gpr_time_similar(gpr_time_from_micros(15, GPR_TIMESPAN),
                                   gpr_time_from_micros(10, GPR_TIMESPAN),
                                   gpr_time_from_micros(10, GPR_TIMESPAN)));
  GPR_ASSERT(0 == gpr_time_similar(gpr_time_from_micros(10, GPR_TIMESPAN),
                                   gpr_time_from_micros(25, GPR_TIMESPAN),
                                   gpr_time_from_micros(10, GPR_TIMESPAN)));
  GPR_ASSERT(0 == gpr_time_similar(gpr_time_from_micros(25, GPR_TIMESPAN),
                                   gpr_time_from_micros(10, GPR_TIMESPAN),
                                   gpr_time_from_micros(10, GPR_TIMESPAN)));
}

static void test_convert_extreme(void) {
  gpr_timespec realtime = {INT64_MAX, 1, GPR_CLOCK_REALTIME};
  gpr_timespec monotime = gpr_convert_clock_type(realtime, GPR_CLOCK_MONOTONIC);
  GPR_ASSERT(monotime.tv_sec == realtime.tv_sec);
  GPR_ASSERT(monotime.clock_type == GPR_CLOCK_MONOTONIC);
}

static void test_cmp_extreme(void) {
  gpr_timespec t1 = {INT64_MAX, 1, GPR_CLOCK_REALTIME};
  gpr_timespec t2 = {INT64_MAX, 2, GPR_CLOCK_REALTIME};
  GPR_ASSERT(gpr_time_cmp(t1, t2) == 0);
  t1.tv_sec = INT64_MIN;
  t2.tv_sec = INT64_MIN;
  GPR_ASSERT(gpr_time_cmp(t1, t2) == 0);
}

int main(int argc, char* argv[]) {
  grpc_test_init(argc, argv);

  test_values();
  test_add_sub();
  test_overflow();
  test_sticky_infinities();
  test_similar();
  test_convert_extreme();
  test_cmp_extreme();
  return 0;
}
