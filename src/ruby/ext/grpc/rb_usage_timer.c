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

#include <ruby/ruby.h>

#include "rb_grpc_imports.generated.h"
#include "rb_usage_timer.h"

#include <grpc/grpc.h>

#include <grpc/support/time.h>
#include <grpc/impl/codegen/alloc.h>
#include <sys/resource.h>
#include <sys/time.h>

#include "rb_grpc.h"

typedef struct grpc_rb_usage_timer {
  struct timeval start_wall_time;
  struct rusage start_cpu_time;
  struct timeval last_sample_wall_time;
  struct rusage last_sample_cpu_time;
} grpc_rb_usage_timer;

/* Timer class for getting elapsed wall, user, and system times. */

static VALUE grpc_rb_cUsageTimer = Qnil;

static void grpc_rb_usage_timer_free(void *p) {
  xfree(p);
}

/* Ruby recognized data type for the UsageTimer class. */
static rb_data_type_t grpc_rb_usage_timer_data_type = {
    "grpc_usage_timer",
    {NULL,
     grpc_rb_usage_timer_free,
     GRPC_RB_MEMSIZE_UNAVAILABLE,
     {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Allocates UsageTimer instances. */
static VALUE grpc_rb_usage_timer_alloc(VALUE cls) {
  grpc_rb_usage_timer *usage_timer =
      gpr_malloc(sizeof(grpc_rb_usage_timer));

  return TypedData_Wrap_Struct(cls, &grpc_rb_usage_timer_data_type, usage_timer);
}

/* Resets the wall, and cpu times to the current elapsed times. */
static VALUE grpc_rb_usage_timer_reset(VALUE self) {
  grpc_rb_usage_timer* usage_timer = NULL;

  TypedData_Get_Struct(self, grpc_rb_usage_timer,
                       &grpc_rb_usage_timer_data_type, usage_timer);

  gettimeofday(&usage_timer->start_wall_time, NULL);
  getrusage(RUSAGE_SELF, &usage_timer->start_cpu_time);

  usage_timer->last_sample_wall_time = usage_timer->start_wall_time;
  usage_timer->last_sample_cpu_time = usage_timer->start_cpu_time;

  return self;
}

/* Calculates the differences between time vals in seconds */
static double grpc_rb_time_diff(struct timeval earlier_time_val, struct timeval later_time_val) {
  struct timeval diff;

  diff.tv_sec = later_time_val.tv_sec - earlier_time_val.tv_sec;
  diff.tv_usec = later_time_val.tv_usec - earlier_time_val.tv_usec;

  return diff.tv_sec + diff.tv_usec * 1e-6;
}

/* Sample the wall, user, and system time. Returns a hash containing the elapsed
 * time of each type since the last reset, or object creation. */
static VALUE grpc_rb_usage_timer_sample(VALUE self) {
  grpc_rb_usage_timer* usage_timer = NULL;
  double elapsed_wall_time;
  double elapsed_user_time;
  double elapsed_system_time;
  VALUE time_sample_hash = Qnil;

  TypedData_Get_Struct(self, grpc_rb_usage_timer,
                       &grpc_rb_usage_timer_data_type, usage_timer);

  gettimeofday(&usage_timer->last_sample_wall_time, NULL);
  getrusage(RUSAGE_SELF, &usage_timer->last_sample_cpu_time);

  elapsed_wall_time = grpc_rb_time_diff(usage_timer->start_wall_time, usage_timer->last_sample_wall_time);
  elapsed_user_time = grpc_rb_time_diff(usage_timer->start_cpu_time.ru_utime, usage_timer->last_sample_cpu_time.ru_utime);
  elapsed_system_time = grpc_rb_time_diff(usage_timer->start_cpu_time.ru_stime, usage_timer->last_sample_cpu_time.ru_stime);

  time_sample_hash = rb_hash_new();

  rb_hash_aset(time_sample_hash, rb_str_new2("wall_time"), rb_float_new(elapsed_wall_time));
  rb_hash_aset(time_sample_hash, rb_str_new2("user_time"), rb_float_new(elapsed_user_time));
  rb_hash_aset(time_sample_hash, rb_str_new2("system_time"), rb_float_new(elapsed_system_time));

  return time_sample_hash;
}

/* Initilize UsageTimer class and start the timers. */
static VALUE grpc_rb_usage_timer_init(VALUE self) {
  grpc_rb_usage_timer_reset(self);

  return self;
}

void Init_grpc_usage_timer() {
  grpc_rb_cUsageTimer = rb_define_class_under(
      grpc_rb_mGrpcCore, "UsageTimer", rb_cObject);

  /* Allocates an object managed by the ruby runtime. */
  rb_define_alloc_func(grpc_rb_cUsageTimer,
                       grpc_rb_usage_timer_alloc);

  rb_define_method(grpc_rb_cUsageTimer, "initialize",
                   grpc_rb_usage_timer_init, 0);

  rb_define_method(grpc_rb_cUsageTimer, "reset",
                   grpc_rb_usage_timer_reset, 0);

  rb_define_method(grpc_rb_cUsageTimer, "sample",
                   grpc_rb_usage_timer_sample, 0);
}
