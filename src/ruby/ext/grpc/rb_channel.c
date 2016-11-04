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
#include "rb_channel.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "rb_grpc.h"
#include "rb_call.h"
#include "rb_channel_args.h"
#include "rb_channel_credentials.h"
#include "rb_completion_queue.h"
#include "rb_server.h"

/* id_channel is the name of the hidden ivar that preserves a reference to the
 * channel on a call, so that calls are not GCed before their channel.  */
static ID id_channel;

/* id_target is the name of the hidden ivar that preserves a reference to the
 * target string used to create the call, preserved so that it does not get
 * GCed before the channel */
static ID id_target;

/* id_insecure_channel is used to indicate that a channel is insecure */
static VALUE id_insecure_channel;

/* grpc_rb_cChannel is the ruby class that proxies grpc_channel. */
static VALUE grpc_rb_cChannel = Qnil;

/* Used during the conversion of a hash to channel args during channel setup */
static VALUE grpc_rb_cChannelArgs;

/* grpc_rb_channel wraps a grpc_channel. */
typedef struct grpc_rb_channel {
  VALUE credentials;

  /* The actual channel */
  grpc_channel *wrapped;
  grpc_completion_queue *queue;
} grpc_rb_channel;

/* Destroys Channel instances. */
static void grpc_rb_channel_free(void *p) {
  grpc_rb_channel *ch = NULL;
  if (p == NULL) {
    return;
  };
  ch = (grpc_rb_channel *)p;

  if (ch->wrapped != NULL) {
    grpc_channel_destroy(ch->wrapped);
    grpc_rb_completion_queue_destroy(ch->queue);
  }

  xfree(p);
}

/* Protects the mark object from GC */
static void grpc_rb_channel_mark(void *p) {
  grpc_rb_channel *channel = NULL;
  if (p == NULL) {
    return;
  }
  channel = (grpc_rb_channel *)p;
  if (channel->credentials != Qnil) {
    rb_gc_mark(channel->credentials);
  }
}

static rb_data_type_t grpc_channel_data_type = {
    "grpc_channel",
    {grpc_rb_channel_mark, grpc_rb_channel_free, GRPC_RB_MEMSIZE_UNAVAILABLE,
     {NULL, NULL}},
    NULL, NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    RUBY_TYPED_FREE_IMMEDIATELY
#endif
};

/* Allocates grpc_rb_channel instances. */
static VALUE grpc_rb_channel_alloc(VALUE cls) {
  grpc_rb_channel *wrapper = ALLOC(grpc_rb_channel);
  wrapper->wrapped = NULL;
  wrapper->credentials = Qnil;
  return TypedData_Wrap_Struct(cls, &grpc_channel_data_type, wrapper);
}

/*
  call-seq:
    insecure_channel = Channel:new("myhost:8080", {'arg1': 'value1'},
                                   :this_channel_is_insecure)
    creds = ...
    secure_channel = Channel:new("myhost:443", {'arg1': 'value1'}, creds)

  Creates channel instances. */
static VALUE grpc_rb_channel_init(int argc, VALUE *argv, VALUE self) {
  VALUE channel_args = Qnil;
  VALUE credentials = Qnil;
  VALUE target = Qnil;
  grpc_rb_channel *wrapper = NULL;
  grpc_channel *ch = NULL;
  grpc_channel_credentials *creds = NULL;
  char *target_chars = NULL;
  grpc_channel_args args;
  MEMZERO(&args, grpc_channel_args, 1);

  /* "3" == 3 mandatory args */
  rb_scan_args(argc, argv, "3", &target, &channel_args, &credentials);

  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  target_chars = StringValueCStr(target);
  grpc_rb_hash_convert_to_channel_args(channel_args, &args);
  if (TYPE(credentials) == T_SYMBOL) {
    if (id_insecure_channel != SYM2ID(credentials)) {
      rb_raise(rb_eTypeError,
               "bad creds symbol, want :this_channel_is_insecure");
      return Qnil;
    }
    ch = grpc_insecure_channel_create(target_chars, &args, NULL);
  } else {
    wrapper->credentials = credentials;
    creds = grpc_rb_get_wrapped_channel_credentials(credentials);
    ch = grpc_secure_channel_create(creds, target_chars, &args, NULL);
  }
  if (args.args != NULL) {
    xfree(args.args); /* Allocated by grpc_rb_hash_convert_to_channel_args */
  }
  if (ch == NULL) {
    rb_raise(rb_eRuntimeError, "could not create an rpc channel to target:%s",
             target_chars);
    return Qnil;
  }
  rb_ivar_set(self, id_target, target);
  wrapper->wrapped = ch;
  wrapper->queue = grpc_completion_queue_create(NULL);
  return self;
}

/*
  call-seq:
    insecure_channel = Channel:new("myhost:8080", {'arg1': 'value1'})
    creds = ...
    secure_channel = Channel:new("myhost:443", {'arg1': 'value1'}, creds)

  Creates channel instances. */
static VALUE grpc_rb_channel_get_connectivity_state(int argc, VALUE *argv,
                                                    VALUE self) {
  VALUE try_to_connect = Qfalse;
  grpc_rb_channel *wrapper = NULL;
  grpc_channel *ch = NULL;

  /* "01" == 0 mandatory args, 1 (try_to_connect) is optional */
  rb_scan_args(argc, argv, "01", try_to_connect);

  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  ch = wrapper->wrapped;
  if (ch == NULL) {
    rb_raise(rb_eRuntimeError, "closed!");
    return Qnil;
  }
  return NUM2LONG(
      grpc_channel_check_connectivity_state(ch, (int)try_to_connect));
}

/* Watch for a change in connectivity state.

   Once the channel connectivity state is different from the last observed
   state, tag will be enqueued on cq with success=1

   If deadline expires BEFORE the state is changed, tag will be enqueued on
   the completion queue with success=0 */
static VALUE grpc_rb_channel_watch_connectivity_state(VALUE self,
                                                      VALUE last_state,
                                                      VALUE deadline) {
  grpc_rb_channel *wrapper = NULL;
  grpc_channel *ch = NULL;
  grpc_completion_queue *cq = NULL;

  void *tag = wrapper;

  grpc_event event;

  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  ch = wrapper->wrapped;
  cq = wrapper->queue;
  if (ch == NULL) {
    rb_raise(rb_eRuntimeError, "closed!");
    return Qnil;
  }
  grpc_channel_watch_connectivity_state(
      ch,
      (grpc_connectivity_state)NUM2LONG(last_state),
      grpc_rb_time_timeval(deadline, /* absolute time */ 0),
      cq,
      tag);

  event = rb_completion_queue_pluck(cq, tag,
                                    gpr_inf_future(GPR_CLOCK_REALTIME), NULL);

  if (event.success) {
    return Qtrue;
  } else {
    return Qfalse;
  }
}

/* Create a call given a grpc_channel, in order to call method. The request
   is not sent until grpc_call_invoke is called. */
static VALUE grpc_rb_channel_create_call(VALUE self, VALUE parent,
                                         VALUE mask, VALUE method,
                                         VALUE host, VALUE deadline) {
  VALUE res = Qnil;
  grpc_rb_channel *wrapper = NULL;
  grpc_call *call = NULL;
  grpc_call *parent_call = NULL;
  grpc_channel *ch = NULL;
  grpc_completion_queue *cq = NULL;
  int flags = GRPC_PROPAGATE_DEFAULTS;
  char *method_chars = StringValueCStr(method);
  char *host_chars = NULL;
  if (host != Qnil) {
    host_chars = StringValueCStr(host);
  }
  if (mask != Qnil) {
    flags = NUM2UINT(mask);
  }
  if (parent != Qnil) {
    parent_call = grpc_rb_get_wrapped_call(parent);
  }

  cq = grpc_completion_queue_create(NULL);
  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  ch = wrapper->wrapped;
  if (ch == NULL) {
    rb_raise(rb_eRuntimeError, "closed!");
    return Qnil;
  }

  call = grpc_channel_create_call(ch, parent_call, flags, cq, method_chars,
                                  host_chars, grpc_rb_time_timeval(
                                      deadline,
                                      /* absolute time */ 0), NULL);
  if (call == NULL) {
    rb_raise(rb_eRuntimeError, "cannot create call with method %s",
             method_chars);
    return Qnil;
  }
  res = grpc_rb_wrap_call(call, cq);

  /* Make this channel an instance attribute of the call so that it is not GCed
   * before the call. */
  rb_ivar_set(res, id_channel, self);
  return res;
}


/* Closes the channel, calling it's destroy method */
static VALUE grpc_rb_channel_destroy(VALUE self) {
  grpc_rb_channel *wrapper = NULL;
  grpc_channel *ch = NULL;

  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  ch = wrapper->wrapped;
  if (ch != NULL) {
    grpc_channel_destroy(ch);
    wrapper->wrapped = NULL;
  }

  return Qnil;
}


/* Called to obtain the target that this channel accesses. */
static VALUE grpc_rb_channel_get_target(VALUE self) {
  grpc_rb_channel *wrapper = NULL;
  VALUE res = Qnil;
  char* target = NULL;

  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  target = grpc_channel_get_target(wrapper->wrapped);
  res = rb_str_new2(target);
  gpr_free(target);

  return res;
}

static void Init_grpc_propagate_masks() {
  /* Constants representing call propagation masks in grpc.h */
  VALUE grpc_rb_mPropagateMasks = rb_define_module_under(
      grpc_rb_mGrpcCore, "PropagateMasks");
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
  VALUE grpc_rb_mConnectivityStates = rb_define_module_under(
      grpc_rb_mGrpcCore, "ConnectivityStates");
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
                   grpc_rb_channel_get_connectivity_state,
                   -1);
  rb_define_method(grpc_rb_cChannel, "watch_connectivity_state",
                   grpc_rb_channel_watch_connectivity_state, 4);
  rb_define_method(grpc_rb_cChannel, "create_call",
                   grpc_rb_channel_create_call, 5);
  rb_define_method(grpc_rb_cChannel, "target", grpc_rb_channel_get_target, 0);
  rb_define_method(grpc_rb_cChannel, "destroy", grpc_rb_channel_destroy, 0);
  rb_define_alias(grpc_rb_cChannel, "close", "destroy");

  id_channel = rb_intern("__channel");
  id_target = rb_intern("__target");
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
grpc_channel *grpc_rb_get_wrapped_channel(VALUE v) {
  grpc_rb_channel *wrapper = NULL;
  TypedData_Get_Struct(v, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  return wrapper->wrapped;
}
