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

#include "rb_channel.h"

#include <ruby/ruby.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include "rb_grpc.h"
#include "rb_call.h"
#include "rb_channel_args.h"
#include "rb_completion_queue.h"
#include "rb_credentials.h"
#include "rb_server.h"

/* id_channel is the name of the hidden ivar that preserves a reference to the
 * channel on a call, so that calls are not GCed before their channel.  */
static ID id_channel;

/* id_target is the name of the hidden ivar that preserves a reference to the
 * target string used to create the call, preserved so that it does not get
 * GCed before the channel */
static ID id_target;

/* id_cqueue is the name of the hidden ivar that preserves a reference to the
 * completion queue used to create the call, preserved so that it does not get
 * GCed before the channel */
static ID id_cqueue;

/* grpc_rb_cChannel is the ruby class that proxies grpc_channel. */
static VALUE grpc_rb_cChannel = Qnil;

/* Used during the conversion of a hash to channel args during channel setup */
static VALUE grpc_rb_cChannelArgs;

/* grpc_rb_channel wraps a grpc_channel.  It provides a peer ruby object,
 * 'mark' to minimize copying when a channel is created from ruby. */
typedef struct grpc_rb_channel {
  /* Holder of ruby objects involved in constructing the channel */
  VALUE mark;
  /* The actual channel */
  grpc_channel *wrapped;
} grpc_rb_channel;

/* Destroys Channel instances. */
static void grpc_rb_channel_free(void *p) {
  grpc_rb_channel *ch = NULL;
  if (p == NULL) {
    return;
  };
  ch = (grpc_rb_channel *)p;

  /* Deletes the wrapped object if the mark object is Qnil, which indicates
   * that no other object is the actual owner. */
  if (ch->wrapped != NULL && ch->mark == Qnil) {
    grpc_channel_destroy(ch->wrapped);
    rb_warning("channel gc: destroyed the c channel");
  } else {
    rb_warning("channel gc: did not destroy the c channel");
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
  if (channel->mark != Qnil) {
    rb_gc_mark(channel->mark);
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
  wrapper->mark = Qnil;
  return TypedData_Wrap_Struct(cls, &grpc_channel_data_type, wrapper);
}

/*
  call-seq:
    insecure_channel = Channel:new("myhost:8080", {'arg1': 'value1'})
    creds = ...
    secure_channel = Channel:new("myhost:443", {'arg1': 'value1'}, creds)

  Creates channel instances. */
static VALUE grpc_rb_channel_init(int argc, VALUE *argv, VALUE self) {
  VALUE channel_args = Qnil;
  VALUE credentials = Qnil;
  VALUE target = Qnil;
  grpc_rb_channel *wrapper = NULL;
  grpc_credentials *creds = NULL;
  grpc_channel *ch = NULL;
  char *target_chars = NULL;
  grpc_channel_args args;
  MEMZERO(&args, grpc_channel_args, 1);

  /* "21" == 2 mandatory args, 1 (credentials) is optional */
  rb_scan_args(argc, argv, "21", &target, &channel_args, &credentials);

  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  target_chars = StringValueCStr(target);
  grpc_rb_hash_convert_to_channel_args(channel_args, &args);
  if (credentials == Qnil) {
    ch = grpc_insecure_channel_create(target_chars, &args, NULL);
  } else {
    creds = grpc_rb_get_wrapped_credentials(credentials);
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
                                                      VALUE cqueue,
                                                      VALUE deadline,
                                                      VALUE tag) {
  grpc_rb_channel *wrapper = NULL;
  grpc_channel *ch = NULL;
  grpc_completion_queue *cq = NULL;

  cq = grpc_rb_get_wrapped_completion_queue(cqueue);
  TypedData_Get_Struct(self, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  ch = wrapper->wrapped;
  if (ch == NULL) {
    rb_raise(rb_eRuntimeError, "closed!");
    return Qnil;
  }
  grpc_channel_watch_connectivity_state(
      ch,
      NUM2LONG(last_state),
      grpc_rb_time_timeval(deadline, /* absolute time */ 0),
      cq,
      ROBJECT(tag));

  return Qnil;
}

/* Clones Channel instances.

   Gives Channel a consistent implementation of Ruby's object copy/dup
   protocol. */
static VALUE grpc_rb_channel_init_copy(VALUE copy, VALUE orig) {
  grpc_rb_channel *orig_ch = NULL;
  grpc_rb_channel *copy_ch = NULL;

  if (copy == orig) {
    return copy;
  }

  /* Raise an error if orig is not a channel object or a subclass. */
  if (TYPE(orig) != T_DATA ||
      RDATA(orig)->dfree != (RUBY_DATA_FUNC)grpc_rb_channel_free) {
    rb_raise(rb_eTypeError, "not a %s", rb_obj_classname(grpc_rb_cChannel));
    return Qnil;
  }

  TypedData_Get_Struct(orig, grpc_rb_channel, &grpc_channel_data_type, orig_ch);
  TypedData_Get_Struct(copy, grpc_rb_channel, &grpc_channel_data_type, copy_ch);

  /* use ruby's MEMCPY to make a byte-for-byte copy of the channel wrapper
   * object. */
  MEMCPY(copy_ch, orig_ch, grpc_rb_channel, 1);
  return copy;
}

/* Create a call given a grpc_channel, in order to call method. The request
   is not sent until grpc_call_invoke is called. */
static VALUE grpc_rb_channel_create_call(VALUE self, VALUE cqueue,
                                         VALUE parent, VALUE mask,
                                         VALUE method, VALUE host,
                                         VALUE deadline) {
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

  cq = grpc_rb_get_wrapped_completion_queue(cqueue);
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
  res = grpc_rb_wrap_call(call);

  /* Make this channel an instance attribute of the call so that it is not GCed
   * before the call. */
  rb_ivar_set(res, id_channel, self);

  /* Make the completion queue an instance attribute of the call so that it is
   * not GCed before the call. */
  rb_ivar_set(res, id_cqueue, cqueue);
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
    wrapper->mark = Qnil;
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
                  LONG2NUM(GRPC_CHANNEL_FATAL_FAILURE));
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
                   grpc_rb_channel_init_copy, 1);

  /* Add ruby analogues of the Channel methods. */
  rb_define_method(grpc_rb_cChannel, "connectivity_state",
                   grpc_rb_channel_get_connectivity_state,
                   -1);
  rb_define_method(grpc_rb_cChannel, "watch_connectivity_state",
                   grpc_rb_channel_watch_connectivity_state, 4);
  rb_define_method(grpc_rb_cChannel, "create_call",
                   grpc_rb_channel_create_call, 6);
  rb_define_method(grpc_rb_cChannel, "target", grpc_rb_channel_get_target, 0);
  rb_define_method(grpc_rb_cChannel, "destroy", grpc_rb_channel_destroy, 0);
  rb_define_alias(grpc_rb_cChannel, "close", "destroy");

  id_channel = rb_intern("__channel");
  id_cqueue = rb_intern("__cqueue");
  id_target = rb_intern("__target");
  rb_define_const(grpc_rb_cChannel, "SSL_TARGET",
                  ID2SYM(rb_intern(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG)));
  rb_define_const(grpc_rb_cChannel, "ENABLE_CENSUS",
                  ID2SYM(rb_intern(GRPC_ARG_ENABLE_CENSUS)));
  rb_define_const(grpc_rb_cChannel, "MAX_CONCURRENT_STREAMS",
                  ID2SYM(rb_intern(GRPC_ARG_MAX_CONCURRENT_STREAMS)));
  rb_define_const(grpc_rb_cChannel, "MAX_MESSAGE_LENGTH",
                  ID2SYM(rb_intern(GRPC_ARG_MAX_MESSAGE_LENGTH)));
  Init_grpc_propagate_masks();
  Init_grpc_connectivity_states();
}

/* Gets the wrapped channel from the ruby wrapper */
grpc_channel *grpc_rb_get_wrapped_channel(VALUE v) {
  grpc_rb_channel *wrapper = NULL;
  TypedData_Get_Struct(v, grpc_rb_channel, &grpc_channel_data_type, wrapper);
  return wrapper->wrapped;
}
