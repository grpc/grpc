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

#include <math.h>
#include <ruby/vm.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "rb_call.h"
#include "rb_call_credentials.h"
#include "rb_channel.h"
#include "rb_channel_credentials.h"
#include "rb_compression_options.h"
#include "rb_event_thread.h"
#include "rb_grpc_imports.generated.h"
#include "rb_loader.h"
#include "rb_server.h"
#include "rb_server_credentials.h"
#include "rb_xds_channel_credentials.h"
#include "rb_xds_server_credentials.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#ifdef GPR_LINUX
#include <sys/syscall.h>
#include <unistd.h>
#endif

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
  rb_undef_alloc_func(grpc_rb_cTimeVal);
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

static bool g_enable_fork_support;

#ifdef GPR_LINUX
static long sys_gettid() { return syscall(__NR_gettid); }
static bool can_enable_fork_support() { return true; }
#else
static long sys_gettid() { return 0; }
static bool can_enable_fork_support() { return false; }
#endif

#if GPR_WINDOWS
static void grpc_ruby_basic_init(void) {}
static bool grpc_ruby_initial_pid(void) { return true; }
static bool grpc_ruby_initial_thread(void) { return true; }
static void grpc_ruby_reset_init_state(void) {}
#else
static pid_t g_init_pid;
static long g_init_tid;

static bool grpc_ruby_initial_pid(void) {
  GPR_ASSERT(g_init_pid != 0);
  return g_init_pid == getpid();
}

static bool grpc_ruby_initial_thread(void) {
  GPR_ASSERT(g_init_tid != 0);
  return sys_gettid() == g_init_tid;
}

static void grpc_ruby_reset_init_state(void) {
  g_init_pid = getpid();
  g_init_tid = sys_gettid();
}

static void grpc_ruby_basic_init(void) {
  GPR_ASSERT(g_init_pid == 0);
  GPR_ASSERT(g_init_tid == 0);
  grpc_ruby_reset_init_state();
  // TODO(apolcyn): ideally, we should share logic with C-core
  // for determining whether or not fork support is enabled, rather
  // than parsing the environment variable ourselves.
  const char* res = getenv("GRPC_ENABLE_FORK_SUPPORT");
  if (res != NULL && strcmp(res, "1") == 0) {
    g_enable_fork_support = can_enable_fork_support();
  }
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
static int64_t g_grpc_rb_prefork_pending;          // synchronized by the GIL
static int64_t g_grpc_rb_num_fork_unsafe_threads;  // synchronized by the GIL

void grpc_ruby_fork_guard() {
  // Check if we're using gRPC between prefork and postfork
  gpr_once_init(&g_once_init, grpc_ruby_basic_init);
  if (g_grpc_rb_prefork_pending) {
    rb_raise(rb_eRuntimeError,
             "grpc cannot be used between calls to GRPC.prefork and "
             "GRPC.postfork_child or GRPC.postfork_parent");
  }
  if (!grpc_ruby_initial_pid()) {
    if (g_enable_fork_support) {
      // Only way we can get here is by enabling for support and forking but not
      // calling prefork
      rb_raise(rb_eRuntimeError,
               "grpc is in a broken state: GRPC.prefork must be called before "
               "calling fork from a process using grpc");
    } else {
      rb_raise(rb_eRuntimeError,
               "grpc cannot be used before and after forking unless the "
               "GRPC_ENABLE_FORK_SUPPORT env var is set to \"1\" and the "
               "platform supports it (linux only)");
    }
  }
}

static VALUE g_bg_thread_init_rb_mu = Qundef;
static bool g_bg_thread_init_done;

static void grpc_ruby_init_threads() {
  // Avoid calling into ruby library (when creating threads here)
  // in gpr_once_init. In general, it appears to be unsafe to call
  // into the ruby library while holding a non-ruby mutex, because a gil yield
  // could end up trying to lock onto that same mutex and deadlocking.
  gpr_log(GPR_INFO,
          "GRPC_RUBY: grpc_ruby_init_threads g_bg_thread_init_done=%d",
          g_bg_thread_init_done);
  rb_mutex_lock(g_bg_thread_init_rb_mu);
  if (!g_bg_thread_init_done) {
    grpc_rb_event_queue_thread_start();
    grpc_rb_channel_polling_thread_start();
    g_bg_thread_init_done = true;
  }
  rb_mutex_unlock(g_bg_thread_init_rb_mu);
}

static int64_t g_grpc_ruby_init_count;

void grpc_ruby_init() {
  gpr_once_init(&g_once_init, grpc_ruby_basic_init);
  grpc_ruby_fork_guard();
  grpc_init();
  grpc_ruby_init_threads();
  // (only gpr_log after logging has been initialized)
  gpr_log(GPR_DEBUG,
          "GRPC_RUBY: grpc_ruby_init - g_enable_fork_support=%d prev "
          "g_grpc_ruby_init_count:%" PRId64,
          g_enable_fork_support, g_grpc_ruby_init_count++);
}

// fork APIs, useable on linux with env var: GRPC_ENABLE_FORK_SUPPORT=1
//
// Must be called once and only once before forking. Must be called on the
// same threads that gRPC was (lazy-)initialized on. One must not call
// into the gRPC library during or after prefork has been called, until
// the corresponding postfork_{parent,child} APIs have been called.
static VALUE grpc_rb_prefork(VALUE self) {
  // This might be the first time we've called into the grpc library, so make
  // sure basic one-time initialization is taken care of. Note that if this is
  // the case, then grpc_init() will start up c-core threads; that's OK since
  // they will be shut down in C-core's pthread_atfork handler.
  gpr_once_init(&g_once_init, grpc_ruby_basic_init);
  grpc_init();
  if (!g_enable_fork_support) {
    rb_raise(rb_eRuntimeError,
             "forking with gRPC/Ruby is only supported on linux with env var: "
             "GRPC_ENABLE_FORK_SUPPORT=1");
  }
  if (g_grpc_rb_prefork_pending) {
    rb_raise(rb_eRuntimeError,
             "GRPC.prefork already called without a matching "
             "GRPC.postfork_{parent,child}");
  }
  if (!grpc_ruby_initial_thread()) {
    rb_raise(rb_eRuntimeError,
             "GRPC.prefork and fork need to be called from the same thread "
             "that GRPC was initialized on (GRPC lazy-initializes when when "
             "the first GRPC object is created");
  }
  if (g_grpc_rb_num_fork_unsafe_threads > 0) {
    rb_raise(
        rb_eRuntimeError,
        "Detected at least %ld threads actively using grpc, so it is not safe "
        "call GRPC.prefork or fork. Note that grpc-ruby servers and "
        "bidirectional "
        "streams manage background threads and are not fork safe.",
        g_grpc_rb_num_fork_unsafe_threads);
  }
  g_grpc_rb_prefork_pending = true;
  rb_mutex_lock(g_bg_thread_init_rb_mu);
  if (g_bg_thread_init_done) {
    grpc_rb_channel_polling_thread_stop();
    grpc_rb_event_queue_thread_stop();
    // all ruby-level background threads joined at this point
    g_bg_thread_init_done = false;
  }
  rb_mutex_unlock(g_bg_thread_init_rb_mu);
  return Qnil;
}

static VALUE grpc_rb_postfork_child(VALUE self) {
  if (!g_grpc_rb_prefork_pending) {
    rb_raise(rb_eRuntimeError,
             "GRPC::postfork_child can only be called once following a "
             "GRPC::prefork");
  }
  if (grpc_ruby_initial_pid()) {
    rb_raise(rb_eRuntimeError,
             "GRPC.postfork_child must be called only from the child process "
             "after a fork");
  }
  grpc_ruby_reset_init_state();
  grpc_ruby_init_threads();
  g_grpc_rb_prefork_pending = false;
  return Qnil;
}

static VALUE grpc_rb_postfork_parent(VALUE self) {
  // TODO(apolcyn): check calling thread vs. thread that gRPC was initialized on
  if (!g_grpc_rb_prefork_pending) {
    rb_raise(rb_eRuntimeError,
             "GRPC::postfork_parent can only be called once following a "
             "GRPC::prefork");
  }
  if (!grpc_ruby_initial_thread()) {
    rb_raise(rb_eRuntimeError,
             "GRPC.postfork_parent needs to be called from the same thread "
             "that GRPC.prefork (and fork) was called from");
  }
  if (!grpc_ruby_initial_pid()) {
    rb_raise(rb_eRuntimeError,
             "GRPC.postfork_parent must be called only from the parent process "
             "after a fork");
  }
  grpc_ruby_init_threads();
  g_grpc_rb_prefork_pending = false;
  return Qnil;
}

// APIs to mark fork-unsafe sections from C-extension code
void grpc_rb_fork_unsafe_begin() { g_grpc_rb_num_fork_unsafe_threads++; }

void grpc_rb_fork_unsafe_end() { g_grpc_rb_num_fork_unsafe_threads--; }

// APIs to mark fork-unsafe sections from ruby code
static VALUE grpc_rb_fork_unsafe_begin_api() { grpc_rb_fork_unsafe_begin(); }

static VALUE grpc_rb_fork_unsafe_end_api() { grpc_rb_fork_unsafe_end(); }

// One-time initialization
void Init_grpc_c() {
  if (!grpc_rb_load_core()) {
    rb_raise(rb_eLoadError, "Couldn't find or load gRPC's dynamic C core");
    return;
  }

  rb_global_variable(&g_bg_thread_init_rb_mu);
  g_bg_thread_init_rb_mu = rb_mutex_new();

  grpc_rb_mGRPC = rb_define_module("GRPC");
  grpc_rb_mGrpcCore = rb_define_module_under(grpc_rb_mGRPC, "Core");
  grpc_rb_sNewServerRpc = rb_struct_define(
      "NewServerRpc", "method", "host", "deadline", "metadata", "call", NULL);
  grpc_rb_sStatus = rb_const_get(rb_cStruct, rb_intern("Status"));
  sym_code = ID2SYM(rb_intern("code"));
  sym_details = ID2SYM(rb_intern("details"));
  sym_metadata = ID2SYM(rb_intern("metadata"));
  // init C-defined classes
  Init_grpc_channel();
  Init_grpc_call();
  Init_grpc_call_credentials();
  Init_grpc_channel_credentials();
  Init_grpc_xds_channel_credentials();
  Init_grpc_server();
  Init_grpc_server_credentials();
  Init_grpc_xds_server_credentials();
  Init_grpc_time_consts();
  Init_grpc_compression_options();
  // define fork APIs
  rb_define_module_function(grpc_rb_mGRPC, "prefork", grpc_rb_prefork, 0);
  rb_define_module_function(grpc_rb_mGRPC, "postfork_child",
                            grpc_rb_postfork_child, 0);
  rb_define_module_function(grpc_rb_mGRPC, "postfork_parent",
                            grpc_rb_postfork_parent, 0);
  rb_define_module_function(grpc_rb_mGrpcCore, "fork_unsafe_begin",
                            grpc_rb_fork_unsafe_begin_api, 0);
  rb_define_module_function(grpc_rb_mGrpcCore, "fork_unsafe_end",
                            grpc_rb_fork_unsafe_end_api, 0);
}
