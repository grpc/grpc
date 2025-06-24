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

#include "rb_channel.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <ruby/thread.h>

#include "rb_byte_buffer.h"
#include "rb_call.h"
#include "rb_channel_args.h"
#include "rb_channel_credentials.h"
#include "rb_completion_queue.h"
#include "rb_grpc.h"
#include "rb_grpc_imports.generated.h"
#include "rb_server.h"
#include "rb_xds_channel_credentials.h"

/* id_channel is the name of the hidden ivar that preserves a reference to the
 * channel on a call, so that calls are not GCed before their channel.  */
static ID id_channel;

/* id_target is the name of the hidden ivar that preserves a reference to the
 * target string used to create the call, preserved so that it does not get
 * GCed before the channel */
static ID id_target;

/* hidden ivar that synchronizes post-fork channel re-creation */
static ID id_channel_recreation_mu;

/* id_insecure_channel is used to indicate that a channel is insecure */
static VALUE id_insecure_channel;

/* grpc_rb_cChannel is the ruby class that proxies grpc_channel. */
static VALUE grpc_rb_cChannel = Qnil;

/* Used during the conversion of a hash to channel args during channel setup */
static VALUE grpc_rb_cChannelArgs;

/* grpc_rb_channel wraps a grpc_channel. */
typedef struct grpc_rb_channel {
  grpc_channel* channel;
} grpc_rb_channel;

static void grpc_rb_channel_free(void* p) {
  if (p == NULL) {
    return;
  };
  grpc_rb_channel* wrapper = (grpc_rb_channel*)p;
  if (wrapper->channel != NULL) {
    grpc_channel_destroy(wrapper->channel);
    wrapper->channel = NULL;
  }
  xfree(p);
}

static rb_data_type_t grpc_channel_data_type = {
    "grpc_channel",
    {NULL, grpc_rb_channel_free, GRPC_RB_MEMSIZE_UNAVAILABLE, {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Allocates grpc_rb_channel instances. */
static VALUE grpc_rb_channel_alloc(VALUE cls) {
  grpc_ruby_init();
  grpc_rb_channel* wrapper = ALLOC(grpc_rb_channel);
  wrapper->channel = NULL;
  return TypedData_Wrap_Struct(cls, &grpc_channel_data_type, wrapper);
}

/*
  call-seq:
    insecure_channel = Channel:new("myhost:8080", {'arg1': 'value1'},
                                   :this_channel_is_insecure)
    creds = ...
    secure_channel = Channel:new("myhost:443", {'arg1': 'value1'}, creds)

  Creates channel instances. */
static VALUE grpc_rb_channel_init(int argc, VALUE* argv, VALUE self) {
  VALUE rb_channel_args = Qnil;
  VALUE rb_credentials = Qnil;
  VALUE target = Qnil;
  grpc_rb_channel* wrapper = NULL;
  char* target_chars = NULL;
  grpc_ruby_fork_guard();
  /* "3" == 3 mandatory args */
  rb_scan_args(argc, argv, "3", &target, &rb_channel_args, &rb_credentials);
  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  target_chars = StringValueCStr(target);
  grpc_channel_args channel_args;
  memset(&channel_args, 0, sizeof(channel_args));
  grpc_rb_hash_convert_to_channel_args(rb_channel_args, &channel_args);
  if (TYPE(rb_credentials) == T_SYMBOL) {
    if (id_insecure_channel != SYM2ID(rb_credentials)) {
      rb_raise(rb_eTypeError,
               "bad creds symbol, want :this_channel_is_insecure");
      return Qnil;
    }
    grpc_channel_credentials* insecure_creds =
        grpc_insecure_credentials_create();
    wrapper->channel =
        grpc_channel_create(target_chars, insecure_creds, &channel_args);
    grpc_channel_credentials_release(insecure_creds);
  } else {
    grpc_channel_credentials* creds;
    if (grpc_rb_is_channel_credentials(rb_credentials)) {
      creds = grpc_rb_get_wrapped_channel_credentials(rb_credentials);
    } else if (grpc_rb_is_xds_channel_credentials(rb_credentials)) {
      creds = grpc_rb_get_wrapped_xds_channel_credentials(rb_credentials);
    } else {
      rb_raise(rb_eTypeError,
               "bad creds, want ChannelCredentials or XdsChannelCredentials");
      return Qnil;
    }
    wrapper->channel = grpc_channel_create(target_chars, creds, &channel_args);
  }
  grpc_rb_channel_args_destroy(&channel_args);
  if (wrapper->channel == NULL) {
    rb_raise(rb_eRuntimeError, "could not create an rpc channel to target:%s",
             target_chars);
    return Qnil;
  }
  rb_ivar_set(self, id_target, target);
  rb_ivar_set(self, id_channel_recreation_mu, rb_mutex_new());
  return self;
}

/*
  call-seq:
    ch.connectivity_state       -> state
    ch.connectivity_state(true) -> state

  Indicates the current state of the channel, whose value is one of the
  constants defined in GRPC::Core::ConnectivityStates.

  It also tries to connect if the channel is idle in the second form. */
static VALUE grpc_rb_channel_get_connectivity_state(int argc, VALUE* argv,
                                                    VALUE self) {
  VALUE try_to_connect_param = Qfalse;
  grpc_rb_channel* wrapper = NULL;
  /* "01" == 0 mandatory args, 1 (try_to_connect) is optional */
  rb_scan_args(argc, argv, "01", &try_to_connect_param);
  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  if (wrapper->channel == NULL) {
    rb_raise(rb_eRuntimeError, "closed!");
    return Qnil;
  }
  bool try_to_connect = RTEST(try_to_connect_param) ? true : false;
  int state =
      grpc_channel_check_connectivity_state(wrapper->channel, try_to_connect);
  return LONG2NUM(state);
}

/* Wait until the channel's connectivity state becomes different from
 * "last_state", or "deadline" expires.
 * Returns true if the channel's connectivity state becomes different
 * from "last_state" within "deadline".
 * Returns false if "deadline" expires before the channel's connectivity
 * state changes from "last_state".
 * */
static VALUE grpc_rb_channel_watch_connectivity_state(VALUE self,
                                                      VALUE last_state,
                                                      VALUE rb_deadline) {
  grpc_rb_channel* wrapper = NULL;
  grpc_ruby_fork_guard();
  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  if (wrapper->channel == NULL) {
    rb_raise(rb_eRuntimeError, "closed!");
    return Qnil;
  }
  if (!FIXNUM_P(last_state)) {
    rb_raise(
        rb_eTypeError,
        "bad type for last_state. want a GRPC::Core::ChannelState constant");
    return Qnil;
  }
  const void* tag = &wrapper;
  gpr_timespec deadline = grpc_rb_time_timeval(rb_deadline, 0);
  grpc_completion_queue* cq = grpc_completion_queue_create_for_pluck(NULL);
  grpc_channel_watch_connectivity_state(wrapper->channel, NUM2LONG(last_state),
                                        deadline, cq, tag);
  grpc_event event =
      rb_completion_queue_pluck(cq, tag, gpr_inf_future(GPR_CLOCK_REALTIME),
                                "grpc_channel_watch_connectivity_state");
  // TODO(apolcyn): this CQ would leak if the thread were killed
  // while polling queue_pluck, e.g. with Thread#kill. One fix may be
  // to make this CQ owned by the channel object. Another fix could be to
  // busy-poll watch_connectivity_state with a short deadline, without
  // the GIL, rather than just polling CQ pluck, and destroy the CQ
  // before exitting the no-GIL block.
  grpc_completion_queue_shutdown(cq);
  grpc_rb_completion_queue_destroy(cq);
  if (event.type == GRPC_OP_COMPLETE) {
    return Qtrue;
  } else if (event.type == GRPC_QUEUE_TIMEOUT) {
    return Qfalse;
  } else {
    grpc_absl_log_int(
        GPR_ERROR,
        "GRPC_RUBY: unexpected grpc_channel_watch_connectivity_state result:",
        event.type);
    return Qfalse;
  }
}

/* Create a call given a grpc_channel, in order to call method. The request
   is not sent until grpc_call_invoke is called. */
static VALUE grpc_rb_channel_create_call(VALUE self, VALUE parent, VALUE mask,
                                         VALUE method, VALUE host,
                                         VALUE deadline) {
  VALUE res = Qnil;
  grpc_rb_channel* wrapper = NULL;
  grpc_call* call = NULL;
  grpc_call* parent_call = NULL;
  grpc_completion_queue* cq = NULL;
  int flags = GRPC_PROPAGATE_DEFAULTS;
  grpc_slice method_slice;
  grpc_slice host_slice;
  grpc_slice* host_slice_ptr = NULL;
  char* tmp_str = NULL;
  grpc_ruby_fork_guard();
  if (host != Qnil) {
    host_slice =
        grpc_slice_from_copied_buffer(RSTRING_PTR(host), RSTRING_LEN(host));
    host_slice_ptr = &host_slice;
  }
  if (mask != Qnil) {
    flags = NUM2UINT(mask);
  }
  if (parent != Qnil) {
    parent_call = grpc_rb_get_wrapped_call(parent);
  }
  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  if (wrapper->channel == NULL) {
    rb_raise(rb_eRuntimeError, "closed!");
    return Qnil;
  }
  cq = grpc_completion_queue_create_for_pluck(NULL);
  method_slice =
      grpc_slice_from_copied_buffer(RSTRING_PTR(method), RSTRING_LEN(method));
  call = grpc_channel_create_call(wrapper->channel, parent_call, flags, cq,
                                  method_slice, host_slice_ptr,
                                  grpc_rb_time_timeval(deadline,
                                                       /* absolute time */ 0),
                                  NULL);
  if (call == NULL) {
    tmp_str = grpc_slice_to_c_string(method_slice);
    rb_raise(rb_eRuntimeError, "cannot create call with method %s", tmp_str);
    return Qnil;
  }
  grpc_slice_unref(method_slice);
  if (host_slice_ptr != NULL) {
    grpc_slice_unref(host_slice);
  }
  res = grpc_rb_wrap_call(call, cq);
  /* Make this channel an instance attribute of the call so that it is not GCed
   * before the call. */
  rb_ivar_set(res, id_channel, self);
  return res;
}

/* Closes the channel, calling it's destroy method */
/* Note this is an API-level call; a wrapped channel's finalizer doesn't call
 * this */
static VALUE grpc_rb_channel_destroy(VALUE self) {
  grpc_rb_channel* wrapper = NULL;
  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  if (wrapper->channel != NULL) {
    grpc_channel_destroy(wrapper->channel);
    wrapper->channel = NULL;
  }
  return Qnil;
}

/* Called to obtain the target that this channel accesses. */
static VALUE grpc_rb_channel_get_target(VALUE self) {
  grpc_rb_channel* wrapper = NULL;
  VALUE res = Qnil;
  char* target = NULL;
  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  target = grpc_channel_get_target(wrapper->channel);
  res = rb_str_new2(target);
  gpr_free(target);
  return res;
}

static void Init_grpc_propagate_masks() {
  /* Constants representing call propagation masks in grpc.h */
  VALUE grpc_rb_mPropagateMasks =
      rb_define_module_under(grpc_rb_mGrpcCore, "PropagateMasks");
  rb_define_const(grpc_rb_mPropagateMasks, "DEADLINE",
                  UINT2NUM(GRPC_PROPAGATE_DEADLINE));
  rb_define_const(grpc_rb_mPropagateMasks, "CENSUS_STATS_CONTEXT",
                  UINT2NUM(GRPC_PROPAGATE_CENSUS_STATS_CONTEXT));
  rb_define_const(grpc_rb_mPropagateMasks, "CENSUS_TRACING_CONTEXT",
                  UINT2NUM(GRPC_PROPAGATE_CENSUS_TRACING_CONTEXT));
  rb_define_const(grpc_rb_mPropagateMasks, "CANCELLATION",
                  UINT2NUM(GRPC_PROPAGATE_CANCELLATION));
  rb_define_const(grpc_rb_mPropagateMasks, "DEFAULTS",
                  UINT2NUM(GRPC_PROPAGATE_DEFAULTS));
}

static void Init_grpc_connectivity_states() {
  /* Constants representing call propagation masks in grpc.h */
  VALUE grpc_rb_mConnectivityStates =
      rb_define_module_under(grpc_rb_mGrpcCore, "ConnectivityStates");
  rb_define_const(grpc_rb_mConnectivityStates, "IDLE",
                  LONG2NUM(GRPC_CHANNEL_IDLE));
  rb_define_const(grpc_rb_mConnectivityStates, "CONNECTING",
                  LONG2NUM(GRPC_CHANNEL_CONNECTING));
  rb_define_const(grpc_rb_mConnectivityStates, "READY",
                  LONG2NUM(GRPC_CHANNEL_READY));
  rb_define_const(grpc_rb_mConnectivityStates, "TRANSIENT_FAILURE",
                  LONG2NUM(GRPC_CHANNEL_TRANSIENT_FAILURE));
  rb_define_const(grpc_rb_mConnectivityStates, "FATAL_FAILURE",
                  LONG2NUM(GRPC_CHANNEL_SHUTDOWN));
}

void Init_grpc_channel() {
  grpc_rb_cChannelArgs = rb_define_class("TmpChannelArgs", rb_cObject);
  rb_undef_alloc_func(grpc_rb_cChannelArgs);
  grpc_rb_cChannel =
      rb_define_class_under(grpc_rb_mGrpcCore, "Channel", rb_cObject);

  /* Allocates an object managed by the ruby runtime */
  rb_define_alloc_func(grpc_rb_cChannel, grpc_rb_channel_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(grpc_rb_cChannel, "initialize", grpc_rb_channel_init, -1);
  rb_define_method(grpc_rb_cChannel, "initialize_copy",
                   grpc_rb_cannot_init_copy, 1);

  /* Add ruby analogues of the Channel methods. */
  rb_define_method(grpc_rb_cChannel, "connectivity_state",
                   grpc_rb_channel_get_connectivity_state, -1);
  rb_define_method(grpc_rb_cChannel, "watch_connectivity_state",
                   grpc_rb_channel_watch_connectivity_state, 2);
  rb_define_method(grpc_rb_cChannel, "create_call", grpc_rb_channel_create_call,
                   5);
  rb_define_method(grpc_rb_cChannel, "target", grpc_rb_channel_get_target, 0);
  rb_define_method(grpc_rb_cChannel, "destroy", grpc_rb_channel_destroy, 0);
  rb_define_alias(grpc_rb_cChannel, "close", "destroy");

  id_channel = rb_intern("__channel");
  id_target = rb_intern("__target");
  id_channel_recreation_mu = rb_intern("__channel_recreation_mu");
  rb_define_const(grpc_rb_cChannel, "SSL_TARGET",
                  ID2SYM(rb_intern(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG)));
  rb_define_const(grpc_rb_cChannel, "ENABLE_CENSUS",
                  ID2SYM(rb_intern(GRPC_ARG_ENABLE_CENSUS)));
  rb_define_const(grpc_rb_cChannel, "MAX_CONCURRENT_STREAMS",
                  ID2SYM(rb_intern(GRPC_ARG_MAX_CONCURRENT_STREAMS)));
  rb_define_const(grpc_rb_cChannel, "MAX_MESSAGE_LENGTH",
                  ID2SYM(rb_intern(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH)));
  id_insecure_channel = rb_intern("this_channel_is_insecure");
  Init_grpc_propagate_masks();
  Init_grpc_connectivity_states();
}

/* Gets the wrapped channel from the ruby wrapper */
grpc_channel* grpc_rb_get_wrapped_channel(VALUE v) {
  grpc_rb_channel* wrapper = NULL;
  TypedData_Get_Struct(v, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  return wrapper->channel;
}
