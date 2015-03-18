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

#include "rb_call.h"

#include <ruby.h>

#include <grpc/grpc.h>
#include "rb_byte_buffer.h"
#include "rb_completion_queue.h"
#include "rb_metadata.h"
#include "rb_grpc.h"

/* id_cq is the name of the hidden ivar that preserves a reference to a
 * completion queue */
static ID id_cq;

/* id_flags is the name of the hidden ivar that preserves the value of
 * the flags used to create metadata from a Hash */
static ID id_flags;

/* id_input_md is the name of the hidden ivar that preserves the hash used to
 * create metadata, so that references to the strings it contains last as long
 * as the call the metadata is added to. */
static ID id_input_md;

/* id_metadata is name of the attribute used to access the metadata hash
 * received by the call and subsequently saved on it. */
static ID id_metadata;

/* id_status is name of the attribute used to access the status object
 * received by the call and subsequently saved on it. */
static ID id_status;

/* hash_all_calls is a hash of Call address -> reference count that is used to
 * track the creation and destruction of rb_call instances.
 */
static VALUE hash_all_calls;

/* Destroys a Call. */
void grpc_rb_call_destroy(void *p) {
  grpc_call *call = NULL;
  VALUE ref_count = Qnil;
  if (p == NULL) {
    return;
  };
  call = (grpc_call *)p;

  ref_count = rb_hash_aref(hash_all_calls, OFFT2NUM((VALUE)call));
  if (ref_count == Qnil) {
    return; /* No longer in the hash, so already deleted */
  } else if (NUM2UINT(ref_count) == 1) {
    rb_hash_delete(hash_all_calls, OFFT2NUM((VALUE)call));
    grpc_call_destroy(call);
  } else {
    rb_hash_aset(hash_all_calls, OFFT2NUM((VALUE)call),
                 UINT2NUM(NUM2UINT(ref_count) - 1));
  }
}

/* Error code details is a hash containing text strings describing errors */
VALUE rb_error_code_details;

/* Obtains the error detail string for given error code */
const char *grpc_call_error_detail_of(grpc_call_error err) {
  VALUE detail_ref = rb_hash_aref(rb_error_code_details, UINT2NUM(err));
  const char *detail = "unknown error code!";
  if (detail_ref != Qnil) {
    detail = StringValueCStr(detail_ref);
  }
  return detail;
}

/* grpc_rb_call_add_metadata_hash_cb is the hash iteration callback used by
   grpc_rb_call_add_metadata.
*/
int grpc_rb_call_add_metadata_hash_cb(VALUE key, VALUE val, VALUE call_obj) {
  grpc_call *call = NULL;
  grpc_metadata *md = NULL;
  VALUE md_obj = Qnil;
  VALUE md_obj_args[2];
  VALUE flags = rb_ivar_get(call_obj, id_flags);
  grpc_call_error err;
  int array_length;
  int i;

  /* Construct a metadata object from key and value and add it */
  Data_Get_Struct(call_obj, grpc_call, call);
  md_obj_args[0] = key;

  if (TYPE(val) == T_ARRAY) {
    /* If the value is an array, add each value in the array separately */
    array_length = RARRAY_LEN(val);
    for (i = 0; i < array_length; i++) {
      md_obj_args[1] = rb_ary_entry(val, i);
      md_obj = rb_class_new_instance(2, md_obj_args, rb_cMetadata);
      md = grpc_rb_get_wrapped_metadata(md_obj);
      err = grpc_call_add_metadata_old(call, md, NUM2UINT(flags));
      if (err != GRPC_CALL_OK) {
        rb_raise(rb_eCallError, "add metadata failed: %s (code=%d)",
                 grpc_call_error_detail_of(err), err);
        return ST_STOP;
      }
    }
  } else {
    md_obj_args[1] = val;
    md_obj = rb_class_new_instance(2, md_obj_args, rb_cMetadata);
    md = grpc_rb_get_wrapped_metadata(md_obj);
    err = grpc_call_add_metadata_old(call, md, NUM2UINT(flags));
    if (err != GRPC_CALL_OK) {
      rb_raise(rb_eCallError, "add metadata failed: %s (code=%d)",
               grpc_call_error_detail_of(err), err);
      return ST_STOP;
    }
  }

  return ST_CONTINUE;
}

/*
  call-seq:
     call.add_metadata(completion_queue, hash_elements, flags=nil)

  Add metadata elements to the call from a ruby hash, to be sent upon
  invocation. flags is a bit-field combination of the write flags defined
  above.  REQUIRES: grpc_call_invoke/grpc_call_accept have not been
  called on this call.  Produces no events. */

static VALUE grpc_rb_call_add_metadata(int argc, VALUE *argv, VALUE self) {
  VALUE metadata;
  VALUE flags = Qnil;
  ID id_size = rb_intern("size");

  /* "11" == 1 mandatory args, 1 (flags) is optional */
  rb_scan_args(argc, argv, "11", &metadata, &flags);
  if (NIL_P(flags)) {
    flags = UINT2NUM(0); /* Default to no flags */
  }
  if (TYPE(metadata) != T_HASH) {
    rb_raise(rb_eTypeError, "add metadata failed: metadata should be a hash");
    return Qnil;
  }
  if (NUM2UINT(rb_funcall(metadata, id_size, 0)) == 0) {
    return Qnil;
  }
  rb_ivar_set(self, id_flags, flags);
  rb_ivar_set(self, id_input_md, metadata);
  rb_hash_foreach(metadata, grpc_rb_call_add_metadata_hash_cb, self);
  return Qnil;
}

/* Called by clients to cancel an RPC on the server.
   Can be called multiple times, from any thread. */
static VALUE grpc_rb_call_cancel(VALUE self) {
  grpc_call *call = NULL;
  grpc_call_error err;
  Data_Get_Struct(self, grpc_call, call);
  err = grpc_call_cancel(call);
  if (err != GRPC_CALL_OK) {
    rb_raise(rb_eCallError, "cancel failed: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }

  return Qnil;
}

/*
  call-seq:
     call.invoke(completion_queue, tag, flags=nil)

   Invoke the RPC. Starts sending metadata and request headers on the wire.
   flags is a bit-field combination of the write flags defined above.
   REQUIRES: Can be called at most once per call.
             Can only be called on the client.
   Produces a GRPC_INVOKE_ACCEPTED event on completion. */
static VALUE grpc_rb_call_invoke(int argc, VALUE *argv, VALUE self) {
  VALUE cqueue = Qnil;
  VALUE metadata_read_tag = Qnil;
  VALUE finished_tag = Qnil;
  VALUE flags = Qnil;
  grpc_call *call = NULL;
  grpc_completion_queue *cq = NULL;
  grpc_call_error err;

  /* "31" == 3 mandatory args, 1 (flags) is optional */
  rb_scan_args(argc, argv, "31", &cqueue, &metadata_read_tag, &finished_tag,
               &flags);
  if (NIL_P(flags)) {
    flags = UINT2NUM(0); /* Default to no flags */
  }
  cq = grpc_rb_get_wrapped_completion_queue(cqueue);
  Data_Get_Struct(self, grpc_call, call);
  err = grpc_call_invoke_old(call, cq, ROBJECT(metadata_read_tag),
                             ROBJECT(finished_tag), NUM2UINT(flags));
  if (err != GRPC_CALL_OK) {
    rb_raise(rb_eCallError, "invoke failed: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }

  /* Add the completion queue as an instance attribute, prevents it from being
   * GCed until this call object is GCed */
  rb_ivar_set(self, id_cq, cqueue);

  return Qnil;
}

/* Initiate a read on a call. Output event contains a byte buffer with the
   result of the read.
   REQUIRES: No other reads are pending on the call. It is only safe to start
   the next read after the corresponding read event is received. */
static VALUE grpc_rb_call_start_read(VALUE self, VALUE tag) {
  grpc_call *call = NULL;
  grpc_call_error err;
  Data_Get_Struct(self, grpc_call, call);
  err = grpc_call_start_read_old(call, ROBJECT(tag));
  if (err != GRPC_CALL_OK) {
    rb_raise(rb_eCallError, "start read failed: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }

  return Qnil;
}

/*
  call-seq:
    status = call.status

    Gets the status object saved the call.  */
static VALUE grpc_rb_call_get_status(VALUE self) {
  return rb_ivar_get(self, id_status);
}

/*
  call-seq:
    call.status = status

    Saves a status object on the call.  */
static VALUE grpc_rb_call_set_status(VALUE self, VALUE status) {
  if (!NIL_P(status) && rb_obj_class(status) != rb_sStatus) {
    rb_raise(rb_eTypeError, "bad status: got:<%s> want: <Struct::Status>",
             rb_obj_classname(status));
    return Qnil;
  }

  return rb_ivar_set(self, id_status, status);
}

/*
  call-seq:
    metadata = call.metadata

    Gets the metadata object saved the call.  */
static VALUE grpc_rb_call_get_metadata(VALUE self) {
  return rb_ivar_get(self, id_metadata);
}

/*
  call-seq:
    call.metadata = metadata

    Saves the metadata hash on the call.  */
static VALUE grpc_rb_call_set_metadata(VALUE self, VALUE metadata) {
  if (!NIL_P(metadata) && TYPE(metadata) != T_HASH) {
    rb_raise(rb_eTypeError, "bad metadata: got:<%s> want: <Hash>",
             rb_obj_classname(metadata));
    return Qnil;
  }

  return rb_ivar_set(self, id_metadata, metadata);
}

/*
  call-seq:
     call.start_write(byte_buffer, tag, flags=nil)

   Queue a byte buffer for writing.
   flags is a bit-field combination of the write flags defined above.
   A write with byte_buffer null is allowed, and will not send any bytes on the
   wire. If this is performed without GRPC_WRITE_BUFFER_HINT flag it provides
   a mechanism to flush any previously buffered writes to outgoing flow control.
   REQUIRES: No other writes are pending on the call. It is only safe to
             start the next write after the corresponding write_accepted event
             is received.
             GRPC_INVOKE_ACCEPTED must have been received by the application
             prior to calling this on the client. On the server,
             grpc_call_accept must have been called successfully.
   Produces a GRPC_WRITE_ACCEPTED event. */
static VALUE grpc_rb_call_start_write(int argc, VALUE *argv, VALUE self) {
  VALUE byte_buffer = Qnil;
  VALUE tag = Qnil;
  VALUE flags = Qnil;
  grpc_call *call = NULL;
  grpc_byte_buffer *bfr = NULL;
  grpc_call_error err;

  /* "21" == 2 mandatory args, 1 (flags) is optional */
  rb_scan_args(argc, argv, "21", &byte_buffer, &tag, &flags);
  if (NIL_P(flags)) {
    flags = UINT2NUM(0); /* Default to no flags */
  }
  bfr = grpc_rb_get_wrapped_byte_buffer(byte_buffer);
  Data_Get_Struct(self, grpc_call, call);
  err = grpc_call_start_write_old(call, bfr, ROBJECT(tag), NUM2UINT(flags));
  if (err != GRPC_CALL_OK) {
    rb_raise(rb_eCallError, "start write failed: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }

  return Qnil;
}

/* Queue a status for writing.

   call-seq:
      tag = Object.new
      call.write_status(200, "OK", tag)

   REQUIRES: No other writes are pending on the call. It is only safe to
   start the next write after the corresponding write_accepted event
   is received.
   GRPC_INVOKE_ACCEPTED must have been received by the application
   prior to calling this.
   Only callable on the server.
   Produces a GRPC_FINISHED event when the status is sent and the stream is
   fully closed */
static VALUE grpc_rb_call_start_write_status(VALUE self, VALUE code,
                                             VALUE status, VALUE tag) {
  grpc_call *call = NULL;
  grpc_call_error err;
  Data_Get_Struct(self, grpc_call, call);
  err = grpc_call_start_write_status_old(call, NUM2UINT(code),
                                         StringValueCStr(status), ROBJECT(tag));
  if (err != GRPC_CALL_OK) {
    rb_raise(rb_eCallError, "start write status: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }

  return Qnil;
}

/* No more messages to send.
   REQUIRES: No other writes are pending on the call. */
static VALUE grpc_rb_call_writes_done(VALUE self, VALUE tag) {
  grpc_call *call = NULL;
  grpc_call_error err;
  Data_Get_Struct(self, grpc_call, call);
  err = grpc_call_writes_done_old(call, ROBJECT(tag));
  if (err != GRPC_CALL_OK) {
    rb_raise(rb_eCallError, "writes done: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }

  return Qnil;
}

/* call-seq:
     call.server_end_initial_metadata(flag)

   Only to be called on servers, before sending messages.
   flags is a bit-field combination of the write flags defined above.

   REQUIRES: Can be called at most once per call.
             Can only be called on the server, must be called after
             grpc_call_server_accept
   Produces no events */
static VALUE grpc_rb_call_server_end_initial_metadata(int argc, VALUE *argv,
                                                      VALUE self) {
  VALUE flags = Qnil;
  grpc_call *call = NULL;
  grpc_call_error err;

  /* "01" == 1 (flags) is optional */
  rb_scan_args(argc, argv, "01", &flags);
  if (NIL_P(flags)) {
    flags = UINT2NUM(0); /* Default to no flags */
  }
  Data_Get_Struct(self, grpc_call, call);
  err = grpc_call_server_end_initial_metadata_old(call, NUM2UINT(flags));
  if (err != GRPC_CALL_OK) {
    rb_raise(rb_eCallError, "end_initial_metadata failed: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }
  return Qnil;
}

/* call-seq:
     call.server_accept(completion_queue, finished_tag)

   Accept an incoming RPC, binding a completion queue to it.
   To be called before sending or receiving messages.

   REQUIRES: Can be called at most once per call.
             Can only be called on the server.
   Produces a GRPC_FINISHED event with finished_tag when the call has been
       completed (there may be other events for the call pending at this
       time) */
static VALUE grpc_rb_call_server_accept(VALUE self, VALUE cqueue,
                                        VALUE finished_tag) {
  grpc_call *call = NULL;
  grpc_completion_queue *cq = grpc_rb_get_wrapped_completion_queue(cqueue);
  grpc_call_error err;
  Data_Get_Struct(self, grpc_call, call);
  err = grpc_call_server_accept_old(call, cq, ROBJECT(finished_tag));
  if (err != GRPC_CALL_OK) {
    rb_raise(rb_eCallError, "server_accept failed: %s (code=%d)",
             grpc_call_error_detail_of(err), err);
  }

  /* Add the completion queue as an instance attribute, prevents it from being
   * GCed until this call object is GCed */
  rb_ivar_set(self, id_cq, cqueue);
  return Qnil;
}

/* rb_cCall is the ruby class that proxies grpc_call. */
VALUE rb_cCall = Qnil;

/* rb_eCallError is the ruby class of the exception thrown during call
   operations; */
VALUE rb_eCallError = Qnil;

void Init_grpc_error_codes() {
  /* Constants representing the error codes of grpc_call_error in grpc.h */
  VALUE rb_RpcErrors = rb_define_module_under(rb_mGrpcCore, "RpcErrors");
  rb_define_const(rb_RpcErrors, "OK", UINT2NUM(GRPC_CALL_OK));
  rb_define_const(rb_RpcErrors, "ERROR", UINT2NUM(GRPC_CALL_ERROR));
  rb_define_const(rb_RpcErrors, "NOT_ON_SERVER",
                  UINT2NUM(GRPC_CALL_ERROR_NOT_ON_SERVER));
  rb_define_const(rb_RpcErrors, "NOT_ON_CLIENT",
                  UINT2NUM(GRPC_CALL_ERROR_NOT_ON_CLIENT));
  rb_define_const(rb_RpcErrors, "ALREADY_ACCEPTED",
                  UINT2NUM(GRPC_CALL_ERROR_ALREADY_ACCEPTED));
  rb_define_const(rb_RpcErrors, "ALREADY_INVOKED",
                  UINT2NUM(GRPC_CALL_ERROR_ALREADY_INVOKED));
  rb_define_const(rb_RpcErrors, "NOT_INVOKED",
                  UINT2NUM(GRPC_CALL_ERROR_NOT_INVOKED));
  rb_define_const(rb_RpcErrors, "ALREADY_FINISHED",
                  UINT2NUM(GRPC_CALL_ERROR_ALREADY_FINISHED));
  rb_define_const(rb_RpcErrors, "TOO_MANY_OPERATIONS",
                  UINT2NUM(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS));
  rb_define_const(rb_RpcErrors, "INVALID_FLAGS",
                  UINT2NUM(GRPC_CALL_ERROR_INVALID_FLAGS));

  /* Add the detail strings to a Hash */
  rb_error_code_details = rb_hash_new();
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_OK),
               rb_str_new2("ok"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR),
               rb_str_new2("unknown error"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR_NOT_ON_SERVER),
               rb_str_new2("not available on a server"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR_NOT_ON_CLIENT),
               rb_str_new2("not available on a client"));
  rb_hash_aset(rb_error_code_details,
               UINT2NUM(GRPC_CALL_ERROR_ALREADY_ACCEPTED),
               rb_str_new2("call is already accepted"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR_ALREADY_INVOKED),
               rb_str_new2("call is already invoked"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR_NOT_INVOKED),
               rb_str_new2("call is not yet invoked"));
  rb_hash_aset(rb_error_code_details,
               UINT2NUM(GRPC_CALL_ERROR_ALREADY_FINISHED),
               rb_str_new2("call is already finished"));
  rb_hash_aset(rb_error_code_details,
               UINT2NUM(GRPC_CALL_ERROR_TOO_MANY_OPERATIONS),
               rb_str_new2("outstanding read or write present"));
  rb_hash_aset(rb_error_code_details, UINT2NUM(GRPC_CALL_ERROR_INVALID_FLAGS),
               rb_str_new2("a bad flag was given"));
  rb_define_const(rb_RpcErrors, "ErrorMessages", rb_error_code_details);
  rb_obj_freeze(rb_error_code_details);
}

void Init_grpc_call() {
  /* CallError inherits from Exception to signal that it is non-recoverable */
  rb_eCallError =
      rb_define_class_under(rb_mGrpcCore, "CallError", rb_eException);
  rb_cCall = rb_define_class_under(rb_mGrpcCore, "Call", rb_cObject);

  /* Prevent allocation or inialization of the Call class */
  rb_define_alloc_func(rb_cCall, grpc_rb_cannot_alloc);
  rb_define_method(rb_cCall, "initialize", grpc_rb_cannot_init, 0);
  rb_define_method(rb_cCall, "initialize_copy", grpc_rb_cannot_init_copy, 1);

  /* Add ruby analogues of the Call methods. */
  rb_define_method(rb_cCall, "server_accept", grpc_rb_call_server_accept, 2);
  rb_define_method(rb_cCall, "server_end_initial_metadata",
                   grpc_rb_call_server_end_initial_metadata, -1);
  rb_define_method(rb_cCall, "add_metadata", grpc_rb_call_add_metadata, -1);
  rb_define_method(rb_cCall, "cancel", grpc_rb_call_cancel, 0);
  rb_define_method(rb_cCall, "invoke", grpc_rb_call_invoke, -1);
  rb_define_method(rb_cCall, "start_read", grpc_rb_call_start_read, 1);
  rb_define_method(rb_cCall, "start_write", grpc_rb_call_start_write, -1);
  rb_define_method(rb_cCall, "start_write_status",
                   grpc_rb_call_start_write_status, 3);
  rb_define_method(rb_cCall, "writes_done", grpc_rb_call_writes_done, 1);
  rb_define_method(rb_cCall, "status", grpc_rb_call_get_status, 0);
  rb_define_method(rb_cCall, "status=", grpc_rb_call_set_status, 1);
  rb_define_method(rb_cCall, "metadata", grpc_rb_call_get_metadata, 0);
  rb_define_method(rb_cCall, "metadata=", grpc_rb_call_set_metadata, 1);

  /* Ids used to support call attributes */
  id_metadata = rb_intern("metadata");
  id_status = rb_intern("status");

  /* Ids used by the c wrapping internals. */
  id_cq = rb_intern("__cq");
  id_flags = rb_intern("__flags");
  id_input_md = rb_intern("__input_md");

  /* The hash for reference counting calls, to ensure they can't be destroyed
   * more than once */
  hash_all_calls = rb_hash_new();
  rb_define_const(rb_cCall, "INTERNAL_ALL_CALLs", hash_all_calls);

  Init_grpc_error_codes();
}

/* Gets the call from the ruby object */
grpc_call *grpc_rb_get_wrapped_call(VALUE v) {
  grpc_call *c = NULL;
  Data_Get_Struct(v, grpc_call, c);
  return c;
}

/* Obtains the wrapped object for a given call */
VALUE grpc_rb_wrap_call(grpc_call *c) {
  VALUE obj = Qnil;
  if (c == NULL) {
    return Qnil;
  }
  obj = rb_hash_aref(hash_all_calls, OFFT2NUM((VALUE)c));
  if (obj == Qnil) { /* Not in the hash add it */
    rb_hash_aset(hash_all_calls, OFFT2NUM((VALUE)c), UINT2NUM(1));
  } else {
    rb_hash_aset(hash_all_calls, OFFT2NUM((VALUE)c),
                 UINT2NUM(NUM2UINT(obj) + 1));
  }
  return Data_Wrap_Struct(rb_cCall, GC_NOT_MARKED, grpc_rb_call_destroy, c);
}
