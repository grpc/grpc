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

#include <ruby/ruby.h>

#include "rb_grpc.h"
#include "rb_grpc_imports.generated.h"

#include <math.h>
#include <ruby/vm.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "rb_call.h"
#include "rb_call_credentials.h"
#include "rb_channel.h"
#include "rb_channel_credentials.h"
#include "rb_compression_options.h"
#include "rb_event_thread.h"
#include "rb_loader.h"
#include "rb_server.h"
#include "rb_server_credentials.h"

static VALUE grpc_rb_cTimeVal = Qnil;

static rb_data_type_t grpc_rb_timespec_data_type = {
    "gpr_timespec",
    {GRPC_RB_GC_NOT_MARKED,
     GRPC_RB_GC_DONT_FREE,
     GRPC_RB_MEMSIZE_UNAVAILABLE,
     {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Alloc func that blocks allocation of a given object by raising an
 * exception. */
VALUE grpc_rb_cannot_alloc(VALUE cls) {
  rb_raise(rb_eTypeError,
           "allocation of %s only allowed from the gRPC native layer",
           rb_class2name(cls));
  return Qnil;
}

/* Init func that fails by raising an exception. */
VALUE grpc_rb_cannot_init(VALUE self) {
  rb_raise(rb_eTypeError,
           "initialization of %s only allowed from the gRPC native layer",
           rb_obj_classname(self));
  return Qnil;
}

/* Init/Clone func that fails by raising an exception. */
VALUE grpc_rb_cannot_init_copy(VALUE copy, VALUE self) {
  (void)self;
  rb_raise(rb_eTypeError, "Copy initialization of %s is not supported",
           rb_obj_classname(copy));
  return Qnil;
}

/* id_tv_{,u}sec are accessor methods on Ruby Time instances. */
static ID id_tv_sec;
static ID id_tv_nsec;

/**
 * grpc_rb_time_timeval creates a timeval from a ruby time object.
 *
 * This func is copied from ruby source, MRI/source/time.c, which is published
 * under the same license as the ruby.h, on which the entire extensions is
 * based.
 */
gpr_timespec grpc_rb_time_timeval(VALUE time, int interval) {
  gpr_timespec t;
  gpr_timespec* time_const;
  const char* tstr = interval ? "time interval" : "time";
  const char* want = " want <secs from epoch>|<Time>|<GRPC::TimeConst.*>";

  t.clock_type = GPR_CLOCK_REALTIME;
  switch (TYPE(time)) {
    case T_DATA:
      if (CLASS_OF(time) == grpc_rb_cTimeVal) {
        TypedData_Get_Struct(time, gpr_timespec, &grpc_rb_timespec_data_type,
                             time_const);
        t = *time_const;
      } else if (CLASS_OF(time) == rb_cTime) {
        t.tv_sec = NUM2INT(rb_funcall(time, id_tv_sec, 0));
        t.tv_nsec = NUM2INT(rb_funcall(time, id_tv_nsec, 0));
      } else {
        rb_raise(rb_eTypeError, "bad input: (%s)->c_timeval, got <%s>,%s", tstr,
                 rb_obj_classname(time), want);
      }
      break;

    case T_FIXNUM:
      t.tv_sec = FIX2LONG(time);
      if (interval && t.tv_sec < 0)
        rb_raise(rb_eArgError, "%s must be positive", tstr);
      t.tv_nsec = 0;
      break;

    case T_FLOAT:
      if (interval && RFLOAT_VALUE(time) < 0.0)
        rb_raise(rb_eArgError, "%s must be positive", tstr);
      else {
        double f, d;

        d = modf(RFLOAT_VALUE(time), &f);
        if (d < 0) {
          d += 1;
          f -= 1;
        }
        t.tv_sec = (int64_t)f;
        if (f != t.tv_sec) {
          rb_raise(rb_eRangeError, "%f out of Time range", RFLOAT_VALUE(time));
        }
        t.tv_nsec = (int)(d * 1e9 + 0.5);
      }
      break;

    case T_BIGNUM:
      t.tv_sec = NUM2LONG(time);
      if (interval && t.tv_sec < 0)
        rb_raise(rb_eArgError, "%s must be positive", tstr);
      t.tv_nsec = 0;
      break;

    default:
      rb_raise(rb_eTypeError, "bad input: (%s)->c_timeval, got <%s>,%s", tstr,
               rb_obj_classname(time), want);
      break;
  }
  return t;
}

static void Init_grpc_status_codes() {
  /* Constants representing the status codes or grpc_status_code in status.h */
  VALUE grpc_rb_mStatusCodes =
      rb_define_module_under(grpc_rb_mGrpcCore, "StatusCodes");
  rb_define_const(grpc_rb_mStatusCodes, "OK", INT2NUM(GRPC_STATUS_OK));
  rb_define_const(grpc_rb_mStatusCodes, "CANCELLED",
                  INT2NUM(GRPC_STATUS_CANCELLED));
  rb_define_const(grpc_rb_mStatusCodes, "UNKNOWN",
                  INT2NUM(GRPC_STATUS_UNKNOWN));
  rb_define_const(grpc_rb_mStatusCodes, "INVALID_ARGUMENT",
                  INT2NUM(GRPC_STATUS_INVALID_ARGUMENT));
  rb_define_const(grpc_rb_mStatusCodes, "DEADLINE_EXCEEDED",
                  INT2NUM(GRPC_STATUS_DEADLINE_EXCEEDED));
  rb_define_const(grpc_rb_mStatusCodes, "NOT_FOUND",
                  INT2NUM(GRPC_STATUS_NOT_FOUND));
  rb_define_const(grpc_rb_mStatusCodes, "ALREADY_EXISTS",
                  INT2NUM(GRPC_STATUS_ALREADY_EXISTS));
  rb_define_const(grpc_rb_mStatusCodes, "PERMISSION_DENIED",
                  INT2NUM(GRPC_STATUS_PERMISSION_DENIED));
  rb_define_const(grpc_rb_mStatusCodes, "UNAUTHENTICATED",
                  INT2NUM(GRPC_STATUS_UNAUTHENTICATED));
  rb_define_const(grpc_rb_mStatusCodes, "RESOURCE_EXHAUSTED",
                  INT2NUM(GRPC_STATUS_RESOURCE_EXHAUSTED));
  rb_define_const(grpc_rb_mStatusCodes, "FAILED_PRECONDITION",
                  INT2NUM(GRPC_STATUS_FAILED_PRECONDITION));
  rb_define_const(grpc_rb_mStatusCodes, "ABORTED",
                  INT2NUM(GRPC_STATUS_ABORTED));
  rb_define_const(grpc_rb_mStatusCodes, "OUT_OF_RANGE",
                  INT2NUM(GRPC_STATUS_OUT_OF_RANGE));
  rb_define_const(grpc_rb_mStatusCodes, "UNIMPLEMENTED",
                  INT2NUM(GRPC_STATUS_UNIMPLEMENTED));
  rb_define_const(grpc_rb_mStatusCodes, "INTERNAL",
                  INT2NUM(GRPC_STATUS_INTERNAL));
  rb_define_const(grpc_rb_mStatusCodes, "UNAVAILABLE",
                  INT2NUM(GRPC_STATUS_UNAVAILABLE));
  rb_define_const(grpc_rb_mStatusCodes, "DATA_LOSS",
                  INT2NUM(GRPC_STATUS_DATA_LOSS));
}

/* id_at is the constructor method of the ruby standard Time class. */
static ID id_at;

/* id_inspect is the inspect method found on various ruby objects. */
static ID id_inspect;

/* id_to_s is the to_s method found on various ruby objects. */
static ID id_to_s;

/* Converts a wrapped time constant to a standard time. */
static VALUE grpc_rb_time_val_to_time(VALUE self) {
  gpr_timespec* time_const = NULL;
  gpr_timespec real_time;
  TypedData_Get_Struct(self, gpr_timespec, &grpc_rb_timespec_data_type,
                       time_const);
  real_time = gpr_convert_clock_type(*time_const, GPR_CLOCK_REALTIME);
  return rb_funcall(rb_cTime, id_at, 2, INT2NUM(real_time.tv_sec),
                    INT2NUM(real_time.tv_nsec / 1000));
}

/* Invokes inspect on the ctime version of the time val. */
static VALUE grpc_rb_time_val_inspect(VALUE self) {
  return rb_funcall(grpc_rb_time_val_to_time(self), id_inspect, 0);
}

/* Invokes to_s on the ctime version of the time val. */
static VALUE grpc_rb_time_val_to_s(VALUE self) {
  return rb_funcall(grpc_rb_time_val_to_time(self), id_to_s, 0);
}

static gpr_timespec zero_realtime;
static gpr_timespec inf_future_realtime;
static gpr_timespec inf_past_realtime;

/* Adds a module with constants that map to gpr's static timeval structs. */
static void Init_grpc_time_consts() {
  VALUE grpc_rb_mTimeConsts =
      rb_define_module_under(grpc_rb_mGrpcCore, "TimeConsts");
  grpc_rb_cTimeVal =
      rb_define_class_under(grpc_rb_mGrpcCore, "TimeSpec", rb_cObject);
  zero_realtime = gpr_time_0(GPR_CLOCK_REALTIME);
  inf_future_realtime = gpr_inf_future(GPR_CLOCK_REALTIME);
  inf_past_realtime = gpr_inf_past(GPR_CLOCK_REALTIME);
  rb_define_const(
      grpc_rb_mTimeConsts, "ZERO",
      TypedData_Wrap_Struct(grpc_rb_cTimeVal, &grpc_rb_timespec_data_type,
                            (void*)&zero_realtime));
  rb_define_const(
      grpc_rb_mTimeConsts, "INFINITE_FUTURE",
      TypedData_Wrap_Struct(grpc_rb_cTimeVal, &grpc_rb_timespec_data_type,
                            (void*)&inf_future_realtime));
  rb_define_const(
      grpc_rb_mTimeConsts, "INFINITE_PAST",
      TypedData_Wrap_Struct(grpc_rb_cTimeVal, &grpc_rb_timespec_data_type,
                            (void*)&inf_past_realtime));
  rb_define_method(grpc_rb_cTimeVal, "to_time", grpc_rb_time_val_to_time, 0);
  rb_define_method(grpc_rb_cTimeVal, "inspect", grpc_rb_time_val_inspect, 0);
  rb_define_method(grpc_rb_cTimeVal, "to_s", grpc_rb_time_val_to_s, 0);
  id_at = rb_intern("at");
  id_inspect = rb_intern("inspect");
  id_to_s = rb_intern("to_s");
  id_tv_sec = rb_intern("tv_sec");
  id_tv_nsec = rb_intern("tv_nsec");
}

#if GPR_WINDOWS
static void grpc_ruby_set_init_pid(void) {}
static bool grpc_ruby_forked_after_init(void) { return false; }
#else
static pid_t grpc_init_pid;

static void grpc_ruby_set_init_pid(void) {
  GPR_ASSERT(grpc_init_pid == 0);
  grpc_init_pid = getpid();
}

static bool grpc_ruby_forked_after_init(void) {
  GPR_ASSERT(grpc_init_pid != 0);
  return grpc_init_pid != getpid();
}
#endif

/* Initialize the GRPC module structs */

/* grpc_rb_sNewServerRpc is the struct that holds new server rpc details. */
VALUE grpc_rb_sNewServerRpc = Qnil;
/* grpc_rb_sStatus is the struct that holds status details. */
VALUE grpc_rb_sStatus = Qnil;

/* Initialize the GRPC module. */
VALUE grpc_rb_mGRPC = Qnil;
VALUE grpc_rb_mGrpcCore = Qnil;

/* cached Symbols for members in Status struct */
VALUE sym_code = Qundef;
VALUE sym_details = Qundef;
VALUE sym_metadata = Qundef;

static gpr_once g_once_init = GPR_ONCE_INIT;

void grpc_ruby_fork_guard() {
  if (grpc_ruby_forked_after_init()) {
    rb_raise(rb_eRuntimeError, "grpc cannot be used before and after forking");
  }
}

static VALUE bg_thread_init_rb_mu = Qundef;
static int bg_thread_init_done = 0;

static void grpc_ruby_init_threads() {
  // Avoid calling into ruby library (when creating threads here)
  // in gpr_once_init. In general, it appears to be unsafe to call
  // into the ruby library while holding a non-ruby mutex, because a gil yield
  // could end up trying to lock onto that same mutex and deadlocking.
  rb_mutex_lock(bg_thread_init_rb_mu);
  if (!bg_thread_init_done) {
    grpc_rb_event_queue_thread_start();
    grpc_rb_channel_polling_thread_start();
    bg_thread_init_done = 1;
  }
  rb_mutex_unlock(bg_thread_init_rb_mu);
}

static int64_t g_grpc_ruby_init_count;

void grpc_ruby_init() {
  gpr_once_init(&g_once_init, grpc_ruby_set_init_pid);
  grpc_init();
  grpc_ruby_init_threads();
  // (only gpr_log after logging has been initialized)
  gpr_log(GPR_DEBUG,
          "GRPC_RUBY: grpc_ruby_init - prev g_grpc_ruby_init_count:%" PRId64,
          g_grpc_ruby_init_count++);
}

void grpc_ruby_shutdown() {
  GPR_ASSERT(g_grpc_ruby_init_count > 0);
  if (!grpc_ruby_forked_after_init()) grpc_shutdown();
  gpr_log(
      GPR_DEBUG,
      "GRPC_RUBY: grpc_ruby_shutdown - prev g_grpc_ruby_init_count:%" PRId64,
      g_grpc_ruby_init_count--);
}

void Init_grpc_c() {
  if (!grpc_rb_load_core()) {
    rb_raise(rb_eLoadError, "Couldn't find or load gRPC's dynamic C core");
    return;
  }

  rb_global_variable(&bg_thread_init_rb_mu);
  bg_thread_init_rb_mu = rb_mutex_new();

  grpc_rb_mGRPC = rb_define_module("GRPC");
  grpc_rb_mGrpcCore = rb_define_module_under(grpc_rb_mGRPC, "Core");
  grpc_rb_sNewServerRpc = rb_struct_define(
      "NewServerRpc", "method", "host", "deadline", "metadata", "call", NULL);
  grpc_rb_sStatus =
      rb_struct_define("Status", "code", "details", "metadata", NULL);
  sym_code = ID2SYM(rb_intern("code"));
  sym_details = ID2SYM(rb_intern("details"));
  sym_metadata = ID2SYM(rb_intern("metadata"));

  Init_grpc_channel();
  Init_grpc_call();
  Init_grpc_call_credentials();
  Init_grpc_channel_credentials();
  Init_grpc_server();
  Init_grpc_server_credentials();
  Init_grpc_status_codes();
  Init_grpc_time_consts();
  Init_grpc_compression_options();
}
