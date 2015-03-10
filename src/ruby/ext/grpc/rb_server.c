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

#include <ruby.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include "rb_call.h"
#include "rb_channel_args.h"
#include "rb_completion_queue.h"
#include "rb_server_credentials.h"
#include "rb_grpc.h"

/* rb_cServer is the ruby class that proxies grpc_server. */
VALUE rb_cServer = Qnil;

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
  if (svr->wrapped != NULL && svr->mark == Qnil) {
    grpc_server_shutdown(svr->wrapped);
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

/* Allocates grpc_rb_server instances. */
static VALUE grpc_rb_server_alloc(VALUE cls) {
  grpc_rb_server *wrapper = ALLOC(grpc_rb_server);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return Data_Wrap_Struct(cls, grpc_rb_server_mark, grpc_rb_server_free,
                          wrapper);
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
  Data_Get_Struct(self, grpc_rb_server, wrapper);
  grpc_rb_hash_convert_to_channel_args(channel_args, &args);
  srv = grpc_server_create(cq, &args);

  if (args.args != NULL) {
    xfree(args.args); /* Allocated by grpc_rb_hash_convert_to_channel_args */
  }
  if (srv == NULL) {
    rb_raise(rb_eRuntimeError, "could not create a gRPC server, not sure why");
  }
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
    rb_raise(rb_eTypeError, "not a %s", rb_obj_classname(rb_cServer));
  }

  Data_Get_Struct(orig, grpc_rb_server, orig_srv);
  Data_Get_Struct(copy, grpc_rb_server, copy_srv);

  /* use ruby's MEMCPY to make a byte-for-byte copy of the server wrapper
     object. */
  MEMCPY(copy_srv, orig_srv, grpc_rb_server, 1);
  return copy;
}

static VALUE grpc_rb_server_request_call(VALUE self, VALUE tag_new) {
  grpc_call_error err;
  grpc_rb_server *s = NULL;
  Data_Get_Struct(self, grpc_rb_server, s);
  if (s->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "closed!");
  } else {
    err = grpc_server_request_call_old(s->wrapped, ROBJECT(tag_new));
    if (err != GRPC_CALL_OK) {
      rb_raise(rb_eCallError, "server request failed: %s (code=%d)",
               grpc_call_error_detail_of(err), err);
    }
  }
  return Qnil;
}

static VALUE grpc_rb_server_start(VALUE self) {
  grpc_rb_server *s = NULL;
  Data_Get_Struct(self, grpc_rb_server, s);
  if (s->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "closed!");
  } else {
    grpc_server_start(s->wrapped);
  }
  return Qnil;
}

static VALUE grpc_rb_server_destroy(VALUE self) {
  grpc_rb_server *s = NULL;
  Data_Get_Struct(self, grpc_rb_server, s);
  if (s->wrapped != NULL) {
    grpc_server_shutdown(s->wrapped);
    grpc_server_destroy(s->wrapped);
    s->wrapped = NULL;
    s->mark = Qnil;
  }
  return Qnil;
}

/*
  call-seq:
    // insecure port
    insecure_server = Server.new(cq, {'arg1': 'value1'})
    insecure_server.add_http2_port('mydomain:7575')

    // secure port
    server_creds = ...
    secure_server = Server.new(cq, {'arg1': 'value1'})
    secure_server.add_http_port('mydomain:7575', server_creds)

    Adds a http2 port to server */
static VALUE grpc_rb_server_add_http2_port(int argc, VALUE *argv, VALUE self) {
  VALUE port = Qnil;
  VALUE rb_creds = Qnil;
  grpc_rb_server *s = NULL;
  grpc_server_credentials *creds = NULL;
  int recvd_port = 0;

  /* "11" == 1 mandatory args, 1 (rb_creds) is optional */
  rb_scan_args(argc, argv, "11", &port, &rb_creds);

  Data_Get_Struct(self, grpc_rb_server, s);
  if (s->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "closed!");
    return Qnil;
  } else if (rb_creds == Qnil) {
    recvd_port = grpc_server_add_http2_port(s->wrapped, StringValueCStr(port));
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
  rb_cServer = rb_define_class_under(rb_mGrpcCore, "Server", rb_cObject);

  /* Allocates an object managed by the ruby runtime */
  rb_define_alloc_func(rb_cServer, grpc_rb_server_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(rb_cServer, "initialize", grpc_rb_server_init, 2);
  rb_define_method(rb_cServer, "initialize_copy", grpc_rb_server_init_copy, 1);

  /* Add the server methods. */
  rb_define_method(rb_cServer, "request_call", grpc_rb_server_request_call, 1);
  rb_define_method(rb_cServer, "start", grpc_rb_server_start, 0);
  rb_define_method(rb_cServer, "destroy", grpc_rb_server_destroy, 0);
  rb_define_alias(rb_cServer, "close", "destroy");
  rb_define_method(rb_cServer, "add_http2_port", grpc_rb_server_add_http2_port,
                   -1);
}

/* Gets the wrapped server from the ruby wrapper */
grpc_server *grpc_rb_get_wrapped_server(VALUE v) {
  grpc_rb_server *wrapper = NULL;
  Data_Get_Struct(v, grpc_rb_server, wrapper);
  return wrapper->wrapped;
}
