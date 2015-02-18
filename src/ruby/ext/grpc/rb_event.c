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

#include "rb_event.h"

#include <ruby.h>

#include <grpc/grpc.h>
#include "rb_grpc.h"
#include "rb_byte_buffer.h"
#include "rb_call.h"
#include "rb_metadata.h"

/* grpc_rb_event wraps a grpc_event.  It provides a peer ruby object,
 * 'mark' to minimize copying when an event is created from ruby. */
typedef struct grpc_rb_event {
  /* Holder of ruby objects involved in constructing the channel */
  VALUE mark;
  /* The actual event */
  grpc_event *wrapped;
} grpc_rb_event;

/* rb_mCompletionType is a ruby module that holds the completion type values */
VALUE rb_mCompletionType = Qnil;

/* Destroys Event instances. */
static void grpc_rb_event_free(void *p) {
  grpc_rb_event *ev = NULL;
  if (p == NULL) {
    return;
  };
  ev = (grpc_rb_event *)p;

  /* Deletes the wrapped object if the mark object is Qnil, which indicates
   * that no other object is the actual owner. */
  if (ev->wrapped != NULL && ev->mark == Qnil) {
    grpc_event_finish(ev->wrapped);
    rb_warning("event gc: destroyed the c event");
  } else {
    rb_warning("event gc: did not destroy the c event");
  }

  xfree(p);
}

/* Protects the mark object from GC */
static void grpc_rb_event_mark(void *p) {
  grpc_rb_event *event = NULL;
  if (p == NULL) {
    return;
  }
  event = (grpc_rb_event *)p;
  if (event->mark != Qnil) {
    rb_gc_mark(event->mark);
  }
}

static VALUE grpc_rb_event_result(VALUE self);

/* Obtains the type of an event. */
static VALUE grpc_rb_event_type(VALUE self) {
  grpc_event *event = NULL;
  grpc_rb_event *wrapper = NULL;
  Data_Get_Struct(self, grpc_rb_event, wrapper);
  if (wrapper->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "finished!");
    return Qnil;
  }

  event = wrapper->wrapped;
  switch (event->type) {
    case GRPC_QUEUE_SHUTDOWN:
      return rb_const_get(rb_mCompletionType, rb_intern("QUEUE_SHUTDOWN"));

    case GRPC_READ:
      return rb_const_get(rb_mCompletionType, rb_intern("READ"));

    case GRPC_WRITE_ACCEPTED:
      grpc_rb_event_result(self); /* validates the result */
      return rb_const_get(rb_mCompletionType, rb_intern("WRITE_ACCEPTED"));

    case GRPC_FINISH_ACCEPTED:
      grpc_rb_event_result(self); /* validates the result */
      return rb_const_get(rb_mCompletionType, rb_intern("FINISH_ACCEPTED"));

    case GRPC_CLIENT_METADATA_READ:
      return rb_const_get(rb_mCompletionType,
                          rb_intern("CLIENT_METADATA_READ"));

    case GRPC_FINISHED:
      return rb_const_get(rb_mCompletionType, rb_intern("FINISHED"));

    case GRPC_SERVER_RPC_NEW:
      return rb_const_get(rb_mCompletionType, rb_intern("SERVER_RPC_NEW"));

    default:
      rb_raise(rb_eRuntimeError, "unrecognized event code for an rpc event:%d",
               event->type);
  }
  return Qnil; /* should not be reached */
}

/* Obtains the tag associated with an event. */
static VALUE grpc_rb_event_tag(VALUE self) {
  grpc_event *event = NULL;
  grpc_rb_event *wrapper = NULL;
  Data_Get_Struct(self, grpc_rb_event, wrapper);
  if (wrapper->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "finished!");
    return Qnil;
  }

  event = wrapper->wrapped;
  if (event->tag == NULL) {
    return Qnil;
  }
  return (VALUE)event->tag;
}

/* Obtains the call associated with an event. */
static VALUE grpc_rb_event_call(VALUE self) {
  grpc_event *event = NULL;
  grpc_rb_event *wrapper = NULL;
  Data_Get_Struct(self, grpc_rb_event, wrapper);
  if (wrapper->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "finished!");
    return Qnil;
  }

  event = wrapper->wrapped;
  if (event->call != NULL) {
    return grpc_rb_wrap_call(event->call);
  }
  return Qnil;
}

/* Obtains the metadata associated with an event. */
static VALUE grpc_rb_event_metadata(VALUE self) {
  grpc_event *event = NULL;
  grpc_rb_event *wrapper = NULL;
  grpc_metadata *metadata = NULL;
  VALUE key = Qnil;
  VALUE new_ary = Qnil;
  VALUE result = Qnil;
  VALUE value = Qnil;
  size_t count = 0;
  size_t i = 0;
  Data_Get_Struct(self, grpc_rb_event, wrapper);
  if (wrapper->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "finished!");
    return Qnil;
  }

  /* Figure out which metadata to read. */
  event = wrapper->wrapped;
  switch (event->type) {
    case GRPC_CLIENT_METADATA_READ:
      count = event->data.client_metadata_read.count;
      metadata = event->data.client_metadata_read.elements;
      break;

    case GRPC_FINISHED:
      count = event->data.finished.metadata_count;
      metadata = event->data.finished.metadata_elements;
      break;

    case GRPC_SERVER_RPC_NEW:
      count = event->data.server_rpc_new.metadata_count;
      metadata = event->data.server_rpc_new.metadata_elements;
      break;

    default:
      rb_raise(rb_eRuntimeError,
               "bug: bad event type metadata. got %d; want %d|%d:%d",
               event->type, GRPC_CLIENT_METADATA_READ, GRPC_FINISHED,
               GRPC_SERVER_RPC_NEW);
      return Qnil;
  }

  result = rb_hash_new();
  for (i = 0; i < count; i++) {
    key = rb_str_new2(metadata[i].key);
    value = rb_hash_aref(result, key);
    if (value == Qnil) {
      value = rb_str_new(metadata[i].value, metadata[i].value_length);
      rb_hash_aset(result, key, value);
    } else if (TYPE(value) == T_ARRAY) {
      /* Add the string to the returned array */
      rb_ary_push(value,
                  rb_str_new(metadata[i].value, metadata[i].value_length));
    } else {
      /* Add the current value with this key and the new one to an array */
      new_ary = rb_ary_new();
      rb_ary_push(new_ary, value);
      rb_ary_push(new_ary,
                  rb_str_new(metadata[i].value, metadata[i].value_length));
      rb_hash_aset(result, key, new_ary);
    }
  }
  return result;
}

/* Obtains the data associated with an event. */
static VALUE grpc_rb_event_result(VALUE self) {
  grpc_event *event = NULL;
  grpc_rb_event *wrapper = NULL;
  Data_Get_Struct(self, grpc_rb_event, wrapper);
  if (wrapper->wrapped == NULL) {
    rb_raise(rb_eRuntimeError, "finished!");
    return Qnil;
  }
  event = wrapper->wrapped;

  switch (event->type) {
    case GRPC_QUEUE_SHUTDOWN:
      return Qnil;

    case GRPC_READ:
      return grpc_rb_byte_buffer_create_with_mark(self, event->data.read);

    case GRPC_FINISH_ACCEPTED:
      if (event->data.finish_accepted == GRPC_OP_OK) {
        return Qnil;
      }
      rb_raise(rb_eEventError, "finish failed, not sure why (code=%d)",
               event->data.finish_accepted);
      break;

    case GRPC_WRITE_ACCEPTED:
      if (event->data.write_accepted == GRPC_OP_OK) {
        return Qnil;
      }
      rb_raise(rb_eEventError, "write failed, not sure why (code=%d)",
               event->data.write_accepted);
      break;

    case GRPC_CLIENT_METADATA_READ:
      return grpc_rb_event_metadata(self);

    case GRPC_FINISHED:
      return rb_struct_new(rb_sStatus, UINT2NUM(event->data.finished.status),
                           (event->data.finished.details == NULL
                                ? Qnil
                                : rb_str_new2(event->data.finished.details)),
                           grpc_rb_event_metadata(self), NULL);
      break;

    case GRPC_SERVER_RPC_NEW:
      return rb_struct_new(
          rb_sNewServerRpc, rb_str_new2(event->data.server_rpc_new.method),
          rb_str_new2(event->data.server_rpc_new.host),
          Data_Wrap_Struct(rb_cTimeVal, GC_NOT_MARKED, GC_DONT_FREE,
                           (void *)&event->data.server_rpc_new.deadline),
          grpc_rb_event_metadata(self), NULL);

    default:
      rb_raise(rb_eRuntimeError, "unrecognized event code for an rpc event:%d",
               event->type);
  }

  return Qfalse;
}

static VALUE grpc_rb_event_finish(VALUE self) {
  grpc_event *event = NULL;
  grpc_rb_event *wrapper = NULL;
  Data_Get_Struct(self, grpc_rb_event, wrapper);
  if (wrapper->wrapped == NULL) { /* already closed  */
    return Qnil;
  }
  event = wrapper->wrapped;
  grpc_event_finish(event);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return Qnil;
}

/* rb_cEvent is the Event class whose instances proxy grpc_event */
VALUE rb_cEvent = Qnil;

/* rb_eEventError is the ruby class of the exception thrown on failures during
   rpc event processing. */
VALUE rb_eEventError = Qnil;

void Init_grpc_event() {
  rb_eEventError =
      rb_define_class_under(rb_mGrpcCore, "EventError", rb_eStandardError);
  rb_cEvent = rb_define_class_under(rb_mGrpcCore, "Event", rb_cObject);

  /* Prevent allocation or inialization from ruby. */
  rb_define_alloc_func(rb_cEvent, grpc_rb_cannot_alloc);
  rb_define_method(rb_cEvent, "initialize", grpc_rb_cannot_init, 0);
  rb_define_method(rb_cEvent, "initialize_copy", grpc_rb_cannot_init_copy, 1);

  /* Accessors for the data available in an event. */
  rb_define_method(rb_cEvent, "call", grpc_rb_event_call, 0);
  rb_define_method(rb_cEvent, "result", grpc_rb_event_result, 0);
  rb_define_method(rb_cEvent, "tag", grpc_rb_event_tag, 0);
  rb_define_method(rb_cEvent, "type", grpc_rb_event_type, 0);
  rb_define_method(rb_cEvent, "finish", grpc_rb_event_finish, 0);
  rb_define_alias(rb_cEvent, "close", "finish");

  /* Constants representing the completion types */
  rb_mCompletionType =
      rb_define_module_under(rb_mGrpcCore, "CompletionType");
  rb_define_const(rb_mCompletionType, "QUEUE_SHUTDOWN",
                  INT2NUM(GRPC_QUEUE_SHUTDOWN));
  rb_define_const(rb_mCompletionType, "OP_COMPLETE", INT2NUM(GRPC_OP_COMPLETE));
  rb_define_const(rb_mCompletionType, "READ", INT2NUM(GRPC_READ));
  rb_define_const(rb_mCompletionType, "WRITE_ACCEPTED",
                  INT2NUM(GRPC_WRITE_ACCEPTED));
  rb_define_const(rb_mCompletionType, "FINISH_ACCEPTED",
                  INT2NUM(GRPC_FINISH_ACCEPTED));
  rb_define_const(rb_mCompletionType, "CLIENT_METADATA_READ",
                  INT2NUM(GRPC_CLIENT_METADATA_READ));
  rb_define_const(rb_mCompletionType, "FINISHED", INT2NUM(GRPC_FINISHED));
  rb_define_const(rb_mCompletionType, "SERVER_RPC_NEW",
                  INT2NUM(GRPC_SERVER_RPC_NEW));
  rb_define_const(rb_mCompletionType, "SERVER_SHUTDOWN",
                  INT2NUM(GRPC_SERVER_SHUTDOWN));
  rb_define_const(rb_mCompletionType, "RESERVED",
                  INT2NUM(GRPC_COMPLETION_DO_NOT_USE));
}

VALUE grpc_rb_new_event(grpc_event *ev) {
  grpc_rb_event *wrapper = ALLOC(grpc_rb_event);
  wrapper->wrapped = ev;
  wrapper->mark = Qnil;
  return Data_Wrap_Struct(rb_cEvent, grpc_rb_event_mark, grpc_rb_event_free,
                          wrapper);
}
