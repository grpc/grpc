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
#include "rb_server.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include "rb_byte_buffer.h"
#include "rb_call.h"
#include "rb_channel_args.h"
#include "rb_completion_queue.h"
#include "rb_grpc.h"
#include "rb_server_credentials.h"

/* grpc_rb_cServer is the ruby class that proxies grpc_server. */
static VALUE grpc_rb_cServer = Qnil;

/* id_at is the constructor method of the ruby standard Time class. */
static ID id_at;

/* id_insecure_server is used to indicate that a server is insecure */
static VALUE id_insecure_server;

/* grpc_rb_server wraps a grpc_server. */
typedef struct grpc_rb_server {
  /* The actual server */
  grpc_server *wrapped;
  grpc_completion_queue *queue;
  gpr_atm shutdown_started;
} grpc_rb_server;

static void destroy_server(grpc_rb_server *server, gpr_timespec deadline) {
  grpc_event ev;
  // This can be started by app or implicitly by GC. Avoid a race between these.
  if (gpr_atm_full_fetch_add(&server->shutdown_started, (gpr_atm)1) == 0) {
    if (server->wrapped != NULL) {
      grpc_server_shutdown_and_notify(server->wrapped, server->queue, NULL);
      ev = rb_completion_queue_pluck(server->queue, NULL, deadline, NULL);
      if (ev.type == GRPC_QUEUE_TIMEOUT) {
        grpc_server_cancel_all_calls(server->wrapped);
        rb_completion_queue_pluck(server->queue, NULL,
                                  gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
      }
      grpc_server_destroy(server->wrapped);
      grpc_rb_completion_queue_destroy(server->queue);
      server->wrapped = NULL;
      server->queue = NULL;
    }
  }
}

/* Destroys server instances. */
static void grpc_rb_server_free(void *p) {
  grpc_rb_server *svr = NULL;
  gpr_timespec deadline;
  if (p == NULL) {
    return;
  };
  svr = (grpc_rb_server *)p;

  deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                          gpr_time_from_seconds(2, GPR_TIMESPAN));

  destroy_server(svr, deadline);

  xfree(p);
}

static const rb_data_type_t grpc_rb_server_data_type = {
    "grpc_server",
    {GRPC_RB_GC_NOT_MARKED,
     grpc_rb_server_free,
     GRPC_RB_MEMSIZE_UNAVAILABLE,
     {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    /* It is unsafe to specify RUBY_TYPED_FREE_IMMEDIATELY because the free
     * function would block and we might want to unlock GVL
     * TODO(yugui) Unlock GVL?
     */
    0,
#endif
};

/* Allocates grpc_rb_server instances. */
static VALUE grpc_rb_server_alloc(VALUE cls) {
  grpc_rb_server *wrapper = ALLOC(grpc_rb_server);
  wrapper->wrapped = NULL;
  wrapper->shutdown_started = (gpr_atm)0;
  return TypedData_Wrap_Struct(cls, &grpc_rb_server_data_type, wrapper);
}

/*
  call-seq:
    server = Server.new({'arg1': 'value1'})

  Initializes server instances. */
static VALUE grpc_rb_server_init(VALUE self, VALUE channel_args) {
  grpc_completion_queue *cq = NULL;
  grpc_rb_server *wrapper = NULL;
  grpc_server *srv = NULL;
  grpc_channel_args args;
  MEMZERO(&args, grpc_channel_args, 1);

  grpc_ruby_once_init();

  cq = grpc_completion_queue_create_for_pluck(NULL);
  TypedData_Get_Struct(self, grpc_rb_server, &grpc_rb_server_data_type,
                       wrapper);
  grpc_rb_hash_convert_to_channel_args(channel_args, &args);
  srv = grpc_server_create(&args, NULL);

  if (args.args != NULL) {
    xfree(args.args); /* Allocated by grpc_rb_hash_convert_to_channel_args */
  }
  if (srv == NULL) {
    rb_raise(rb_eRuntimeError, "could not create a gRPC server, not sure why");
  }
  grpc_server_register_completion_queue(srv, cq, NULL);
  wrapper->wrapped = srv;
  wrapper->queue = cq;

  return self;
}

/* request_call_stack holds various values used by the
 * grpc_rb_server_request_call function */
typedef struct request_call_stack {
  grpc_call_details details;
  grpc_metadata_array md_ary;
} request_call_stack;

/* grpc_request_call_stack_init ensures the request_call_stack is properly
 * initialized */
static void grpc_request_call_stack_init(request_call_stack *st) {
  MEMZERO(st, request_call_stack, 1);
  grpc_metadata_array_init(&st->md_ary);
  grpc_call_details_init(&st->details);
}

/* grpc_request_call_stack_cleanup ensures the request_call_stack is properly
 * cleaned up */
static void grpc_request_call_stack_cleanup(request_call_stack *st) {
  grpc_metadata_array_destroy(&st->md_ary);
  grpc_call_details_destroy(&st->details);
}

/* call-seq:
   server.request_call

   Requests notification of a new call on a server. */
static VALUE grpc_rb_server_request_call(VALUE self) {
  grpc_rb_server *s = NULL;
  grpc_call *call = NULL;
  grpc_event ev;
  grpc_call_error err;
  request_call_stack st;
  VALUE result;
  void *tag = (void *)&st;
  grpc_completion_queue *call_queue =
      grpc_completion_queue_create_for_pluck(NULL);
  gpr_timespec deadline;

  TypedData_Get_Struct(self, grpc_rb_server, &grpc_rb_server_data_type, s);
  if (s->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "destroyed!");
    return Qnil;
  }
  grpc_request_call_stack_init(&st);
  /* call grpc_server_request_call, then wait for it to complete using
   * pluck_event */
  err = grpc_server_request_call(s->wrapped, &call, &st.details, &st.md_ary,
                                 call_queue, s->queue, tag);
  if (err != GRPC_CALL_OK) {
    grpc_request_call_stack_cleanup(&st);
    rb_raise(grpc_rb_eCallError,
             "grpc_server_request_call failed: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
    return Qnil;
  }

  ev = rb_completion_queue_pluck(s->queue, tag,
                                 gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  if (!ev.success) {
    grpc_request_call_stack_cleanup(&st);
    rb_raise(grpc_rb_eCallError, "request_call completion failed");
    return Qnil;
  }

  /* build the NewServerRpc struct result */
  deadline = gpr_convert_clock_type(st.details.deadline, GPR_CLOCK_REALTIME);
  result = rb_struct_new(
      grpc_rb_sNewServerRpc, grpc_rb_slice_to_ruby_string(st.details.method),
      grpc_rb_slice_to_ruby_string(st.details.host),
      rb_funcall(rb_cTime, id_at, 2, INT2NUM(deadline.tv_sec),
                 INT2NUM(deadline.tv_nsec / 1000)),
      grpc_rb_md_ary_to_h(&st.md_ary), grpc_rb_wrap_call(call, call_queue),
      NULL);
  grpc_request_call_stack_cleanup(&st);
  return result;
}

static VALUE grpc_rb_server_start(VALUE self) {
  grpc_rb_server *s = NULL;
  TypedData_Get_Struct(self, grpc_rb_server, &grpc_rb_server_data_type, s);
  if (s->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "destroyed!");
  } else {
    grpc_server_start(s->wrapped);
  }
  return Qnil;
}

/*
  call-seq:
    server = Server.new({'arg1': 'value1'})
    ... // do stuff with server
    ...
    ... // to shutdown the server
    server.destroy()

    ... // to shutdown the server with a timeout
    server.destroy(timeout)

  Destroys server instances. */
static VALUE grpc_rb_server_destroy(int argc, VALUE *argv, VALUE self) {
  VALUE timeout = Qnil;
  gpr_timespec deadline;
  grpc_rb_server *s = NULL;

  /* "01" == 0 mandatory args, 1 (timeout) is optional */
  rb_scan_args(argc, argv, "01", &timeout);
  TypedData_Get_Struct(self, grpc_rb_server, &grpc_rb_server_data_type, s);
  if (TYPE(timeout) == T_NIL) {
    deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
  } else {
    deadline = grpc_rb_time_timeval(timeout, /* absolute time*/ 0);
  }

  destroy_server(s, deadline);

  return Qnil;
}

/*
  call-seq:
    // insecure port
    insecure_server = Server.new(cq, {'arg1': 'value1'})
    insecure_server.add_http2_port('mydomain:50051', :this_port_is_insecure)

    // secure port
    server_creds = ...
    secure_server = Server.new(cq, {'arg1': 'value1'})
    secure_server.add_http_port('mydomain:50051', server_creds)

    Adds a http2 port to server */
static VALUE grpc_rb_server_add_http2_port(VALUE self, VALUE port,
                                           VALUE rb_creds) {
  grpc_rb_server *s = NULL;
  grpc_server_credentials *creds = NULL;
  int recvd_port = 0;

  TypedData_Get_Struct(self, grpc_rb_server, &grpc_rb_server_data_type, s);
  if (s->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "destroyed!");
    return Qnil;
  } else if (TYPE(rb_creds) == T_SYMBOL) {
    if (id_insecure_server != SYM2ID(rb_creds)) {
      rb_raise(rb_eTypeError, "bad creds symbol, want :this_port_is_insecure");
      return Qnil;
    }
    recvd_port =
        grpc_server_add_insecure_http2_port(s->wrapped, StringValueCStr(port));
    if (recvd_port == 0) {
      rb_raise(rb_eRuntimeError,
               "could not add port %s to server, not sure why",
               StringValueCStr(port));
    }
  } else {
    creds = grpc_rb_get_wrapped_server_credentials(rb_creds);
    recvd_port = grpc_server_add_secure_http2_port(
        s->wrapped, StringValueCStr(port), creds);
    if (recvd_port == 0) {
      rb_raise(rb_eRuntimeError,
               "could not add secure port %s to server, not sure why",
               StringValueCStr(port));
    }
  }
  return INT2NUM(recvd_port);
}

void Init_grpc_server() {
  grpc_rb_cServer =
      rb_define_class_under(grpc_rb_mGrpcCore, "Server", rb_cObject);

  /* Allocates an object managed by the ruby runtime */
  rb_define_alloc_func(grpc_rb_cServer, grpc_rb_server_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(grpc_rb_cServer, "initialize", grpc_rb_server_init, 1);
  rb_define_method(grpc_rb_cServer, "initialize_copy", grpc_rb_cannot_init_copy,
                   1);

  /* Add the server methods. */
  rb_define_method(grpc_rb_cServer, "request_call", grpc_rb_server_request_call,
                   0);
  rb_define_method(grpc_rb_cServer, "start", grpc_rb_server_start, 0);
  rb_define_method(grpc_rb_cServer, "destroy", grpc_rb_server_destroy, -1);
  rb_define_alias(grpc_rb_cServer, "close", "destroy");
  rb_define_method(grpc_rb_cServer, "add_http2_port",
                   grpc_rb_server_add_http2_port, 2);
  id_at = rb_intern("at");
  id_insecure_server = rb_intern("this_port_is_insecure");
}

/* Gets the wrapped server from the ruby wrapper */
grpc_server *grpc_rb_get_wrapped_server(VALUE v) {
  grpc_rb_server *wrapper = NULL;
  TypedData_Get_Struct(v, grpc_rb_server, &grpc_rb_server_data_type, wrapper);
  return wrapper->wrapped;
}
