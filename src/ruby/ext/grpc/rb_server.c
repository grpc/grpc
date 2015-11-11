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

#include "rb_server.h"

#include <ruby/ruby.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include "rb_call.h"
#include "rb_channel_args.h"
#include "rb_completion_queue.h"
#include "rb_server_credentials.h"
#include "rb_grpc.h"

/* grpc_rb_cServer is the ruby class that proxies grpc_server. */
static VALUE grpc_rb_cServer = Qnil;

/* id_at is the constructor method of the ruby standard Time class. */
static ID id_at;

/* id_insecure_server is used to indicate that a server is insecure */
static VALUE id_insecure_server;

/* grpc_rb_server wraps a grpc_server.  It provides a peer ruby object,
  'mark' to minimize copying when a server is created from ruby. */
typedef struct grpc_rb_server {
  /* Holder of ruby objects involved in constructing the server */
  VALUE mark;
  /* The actual server */
  grpc_server *wrapped;
} grpc_rb_server;

/* Destroys server instances. */
static void grpc_rb_server_free(void *p) {
  grpc_rb_server *svr = NULL;
  if (p == NULL) {
    return;
  };
  svr = (grpc_rb_server *)p;

  /* Deletes the wrapped object if the mark object is Qnil, which indicates
     that no other object is the actual owner. */
  /* grpc_server_shutdown does not exist. Change this to something that does
     or delete it */
  if (svr->wrapped != NULL && svr->mark == Qnil) {
    // grpc_server_shutdown(svr->wrapped);
    // Aborting to indicate a bug
    abort();
    grpc_server_destroy(svr->wrapped);
  }

  xfree(p);
}

/* Protects the mark object from GC */
static void grpc_rb_server_mark(void *p) {
  grpc_rb_server *server = NULL;
  if (p == NULL) {
    return;
  }
  server = (grpc_rb_server *)p;
  if (server->mark != Qnil) {
    rb_gc_mark(server->mark);
  }
}

static const rb_data_type_t grpc_rb_server_data_type = {
    "grpc_server",
    {grpc_rb_server_mark, grpc_rb_server_free, GRPC_RB_MEMSIZE_UNAVAILABLE,
     {NULL, NULL}},
    NULL,
    NULL,
#ifdef RUBY_TYPED_FREE_IMMEDIATELY
    /* It is unsafe to specify RUBY_TYPED_FREE_IMMEDIATELY because the free function would block
     * and we might want to unlock GVL
     * TODO(yugui) Unlock GVL?
     */
    0,
#endif
};

/* Allocates grpc_rb_server instances. */
static VALUE grpc_rb_server_alloc(VALUE cls) {
  grpc_rb_server *wrapper = ALLOC(grpc_rb_server);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return TypedData_Wrap_Struct(cls, &grpc_rb_server_data_type, wrapper);
}

/*
  call-seq:
    cq = CompletionQueue.new
    server = Server.new(cq, {'arg1': 'value1'})

  Initializes server instances. */
static VALUE grpc_rb_server_init(VALUE self, VALUE cqueue, VALUE channel_args) {
  grpc_completion_queue *cq = NULL;
  grpc_rb_server *wrapper = NULL;
  grpc_server *srv = NULL;
  grpc_channel_args args;
  MEMZERO(&args, grpc_channel_args, 1);
  cq = grpc_rb_get_wrapped_completion_queue(cqueue);
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

  /* Add the cq as the server's mark object. This ensures the ruby cq can't be
     GCed before the server */
  wrapper->mark = cqueue;
  return self;
}

/* Clones Server instances.

   Gives Server a consistent implementation of Ruby's object copy/dup
   protocol. */
static VALUE grpc_rb_server_init_copy(VALUE copy, VALUE orig) {
  grpc_rb_server *orig_srv = NULL;
  grpc_rb_server *copy_srv = NULL;

  if (copy == orig) {
    return copy;
  }

  /* Raise an error if orig is not a server object or a subclass. */
  if (TYPE(orig) != T_DATA ||
      RDATA(orig)->dfree != (RUBY_DATA_FUNC)grpc_rb_server_free) {
    rb_raise(rb_eTypeError, "not a %s", rb_obj_classname(grpc_rb_cServer));
  }

  TypedData_Get_Struct(orig, grpc_rb_server, &grpc_rb_server_data_type,
                       orig_srv);
  TypedData_Get_Struct(copy, grpc_rb_server, &grpc_rb_server_data_type,
                       copy_srv);

  /* use ruby's MEMCPY to make a byte-for-byte copy of the server wrapper
     object. */
  MEMCPY(copy_srv, orig_srv, grpc_rb_server, 1);
  return copy;
}

/* request_call_stack holds various values used by the
 * grpc_rb_server_request_call function */
typedef struct request_call_stack {
  grpc_call_details details;
  grpc_metadata_array md_ary;
} request_call_stack;

/* grpc_request_call_stack_init ensures the request_call_stack is properly
 * initialized */
static void grpc_request_call_stack_init(request_call_stack* st) {
  MEMZERO(st, request_call_stack, 1);
  grpc_metadata_array_init(&st->md_ary);
  grpc_call_details_init(&st->details);
  st->details.method = NULL;
  st->details.host = NULL;
}

/* grpc_request_call_stack_cleanup ensures the request_call_stack is properly
 * cleaned up */
static void grpc_request_call_stack_cleanup(request_call_stack* st) {
  grpc_metadata_array_destroy(&st->md_ary);
  grpc_call_details_destroy(&st->details);
}

/* call-seq:
   cq = CompletionQueue.new
   tag = Object.new
   timeout = 10
   server.request_call(cqueue, tag, timeout)

   Requests notification of a new call on a server. */
static VALUE grpc_rb_server_request_call(VALUE self, VALUE cqueue,
                                         VALUE tag_new, VALUE timeout) {
  grpc_rb_server *s = NULL;
  grpc_call *call = NULL;
  grpc_event ev;
  grpc_call_error err;
  request_call_stack st;
  VALUE result;
  gpr_timespec deadline;
  TypedData_Get_Struct(self, grpc_rb_server, &grpc_rb_server_data_type, s);
  if (s->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "destroyed!");
    return Qnil;
  } else {
    grpc_request_call_stack_init(&st);
    /* call grpc_server_request_call, then wait for it to complete using
     * pluck_event */
    err = grpc_server_request_call(
        s->wrapped, &call, &st.details, &st.md_ary,
        grpc_rb_get_wrapped_completion_queue(cqueue),
        grpc_rb_get_wrapped_completion_queue(cqueue),
        ROBJECT(tag_new));
    if (err != GRPC_CALL_OK) {
      grpc_request_call_stack_cleanup(&st);
      rb_raise(grpc_rb_eCallError,
              "grpc_server_request_call failed: %s (code=%d)",
               grpc_call_error_detail_of(err), err);
      return Qnil;
    }

    ev = grpc_rb_completion_queue_pluck_event(cqueue, tag_new, timeout);
    if (ev.type == GRPC_QUEUE_TIMEOUT) {
      grpc_request_call_stack_cleanup(&st);
      return Qnil;
    }
    if (!ev.success) {
      grpc_request_call_stack_cleanup(&st);
      rb_raise(grpc_rb_eCallError, "request_call completion failed");
      return Qnil;
    }

    /* build the NewServerRpc struct result */
    deadline = gpr_convert_clock_type(st.details.deadline, GPR_CLOCK_REALTIME);
    result = rb_struct_new(
        grpc_rb_sNewServerRpc, rb_str_new2(st.details.method),
        rb_str_new2(st.details.host),
        rb_funcall(rb_cTime, id_at, 2, INT2NUM(deadline.tv_sec),
                   INT2NUM(deadline.tv_nsec)),
        grpc_rb_md_ary_to_h(&st.md_ary), grpc_rb_wrap_call(call), NULL);
    grpc_request_call_stack_cleanup(&st);
    return result;
  }
  return Qnil;
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
    cq = CompletionQueue.new
    server = Server.new(cq, {'arg1': 'value1'})
    ... // do stuff with server
    ...
    ... // to shutdown the server
    server.destroy(cq)

    ... // to shutdown the server with a timeout
    server.destroy(cq, timeout)

  Destroys server instances. */
static VALUE grpc_rb_server_destroy(int argc, VALUE *argv, VALUE self) {
  VALUE cqueue = Qnil;
  VALUE timeout = Qnil;
  grpc_completion_queue *cq = NULL;
  grpc_event ev;
  grpc_rb_server *s = NULL;

  /* "11" == 1 mandatory args, 1 (timeout) is optional */
  rb_scan_args(argc, argv, "11", &cqueue, &timeout);
  cq = grpc_rb_get_wrapped_completion_queue(cqueue);
  TypedData_Get_Struct(self, grpc_rb_server, &grpc_rb_server_data_type, s);

  if (s->wrapped != NULL) {
    grpc_server_shutdown_and_notify(s->wrapped, cq, NULL);
    ev = grpc_rb_completion_queue_pluck_event(cqueue, Qnil, timeout);
    if (!ev.success) {
      rb_warn("server shutdown failed, cancelling the calls, objects may leak");
      grpc_server_cancel_all_calls(s->wrapped);
      return Qfalse;
    }
    grpc_server_destroy(s->wrapped);
    s->wrapped = NULL;
  }
  return Qtrue;
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
      rb_raise(rb_eTypeError,
               "bad creds symbol, want :this_port_is_insecure");
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
    recvd_port =
        grpc_server_add_secure_http2_port(s->wrapped, StringValueCStr(port),
                                          creds);
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
  rb_define_method(grpc_rb_cServer, "initialize", grpc_rb_server_init, 2);
  rb_define_method(grpc_rb_cServer, "initialize_copy",
                   grpc_rb_server_init_copy, 1);

  /* Add the server methods. */
  rb_define_method(grpc_rb_cServer, "request_call",
                   grpc_rb_server_request_call, 3);
  rb_define_method(grpc_rb_cServer, "start", grpc_rb_server_start, 0);
  rb_define_method(grpc_rb_cServer, "destroy", grpc_rb_server_destroy, -1);
  rb_define_alias(grpc_rb_cServer, "close", "destroy");
  rb_define_method(grpc_rb_cServer, "add_http2_port",
                   grpc_rb_server_add_http2_port,
                   2);
  id_at = rb_intern("at");
  id_insecure_server = rb_intern("this_port_is_insecure");
}

/* Gets the wrapped server from the ruby wrapper */
grpc_server *grpc_rb_get_wrapped_server(VALUE v) {
  grpc_rb_server *wrapper = NULL;
  TypedData_Get_Struct(v, grpc_rb_server, &grpc_rb_server_data_type, wrapper);
  return wrapper->wrapped;
}
