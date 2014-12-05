/*
 *
 * Copyright 2014, Google Inc.
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

#include "rb_status.h"

#include <ruby.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/status.h>
#include "rb_grpc.h"

/* grpc_rb_status wraps a grpc_status.  It provides a peer ruby object, 'mark'
 * to minimize copying when a status is created from ruby. */
typedef struct grpc_rb_status {
  /* Holder of ruby objects involved in constructing the status */
  VALUE mark;
  /* The actual status */
  grpc_status *wrapped;
} grpc_rb_status;

/* Destroys Status instances. */
static void grpc_rb_status_free(void *p) {
  grpc_rb_status *status = NULL;
  if (p == NULL) {
    return;
  };
  status = (grpc_rb_status *)p;

  /* Delete the wrapped object if the mark object is Qnil, which indicates that
   * no other object is the actual owner. */
  if (status->wrapped != NULL && status->mark == Qnil) {
    status->mark = Qnil;
    if (status->wrapped->details) {
      xfree(status->wrapped->details);
    }
    xfree(status->wrapped);
  }

  xfree(p);
}

/* Protects the mark object from GC */
static void grpc_rb_status_mark(void *p) {
  grpc_rb_status *status = NULL;
  if (p == NULL) {
    return;
  }
  status = (grpc_rb_status *)p;

  /* If it's not already cleaned up, mark the mark object */
  if (status->mark != Qnil) {
    rb_gc_mark(status->mark);
  }
}

/* Allocates Status instances.

   Provides safe initial defaults for the instance fields. */
static VALUE grpc_rb_status_alloc(VALUE cls) {
  grpc_rb_status *wrapper = ALLOC(grpc_rb_status);
  wrapper->wrapped = NULL;
  wrapper->mark = Qnil;
  return Data_Wrap_Struct(cls, grpc_rb_status_mark, grpc_rb_status_free,
                          wrapper);
}

/* The name of the attribute used on the mark object to hold the details. */
static ID id_details;

/* Initializes Status instances. */
static VALUE grpc_rb_status_init(VALUE self, VALUE code, VALUE details) {
  grpc_rb_status *wrapper = NULL;
  grpc_status *status = NULL;
  Data_Get_Struct(self, grpc_rb_status, wrapper);

  /* Use a direct pointer to the original detail value to avoid copying. Assume
   * that details is null-terminated. */
  status = ALLOC(grpc_status);
  status->details = StringValueCStr(details);
  status->code = NUM2INT(code);
  wrapper->wrapped = status;

  /* Create the mark and add the original details object to it. */
  wrapper->mark = rb_class_new_instance(0, NULL, rb_cObject);
  rb_ivar_set(wrapper->mark, id_details, details);
  return self;
}

/* Clones Status instances.

   Gives Status a consistent implementation of Ruby's object copy/dup
   protocol. */
static VALUE grpc_rb_status_init_copy(VALUE copy, VALUE orig) {
  grpc_rb_status *orig_status = NULL;
  grpc_rb_status *copy_status = NULL;

  if (copy == orig) {
    return copy;
  }

  /* Raise an error if orig is not a Status object or a subclass. */
  if (TYPE(orig) != T_DATA ||
      RDATA(orig)->dfree != (RUBY_DATA_FUNC)grpc_rb_status_free) {
    rb_raise(rb_eTypeError, "not a %s", rb_obj_classname(rb_cStatus));
  }

  Data_Get_Struct(orig, grpc_rb_status, orig_status);
  Data_Get_Struct(copy, grpc_rb_status, copy_status);
  MEMCPY(copy_status, orig_status, grpc_rb_status, 1);
  return copy;
}

/* Gets the Status code. */
static VALUE grpc_rb_status_code(VALUE self) {
  grpc_rb_status *status = NULL;
  Data_Get_Struct(self, grpc_rb_status, status);
  return INT2NUM(status->wrapped->code);
}

/* Gets the Status details. */
static VALUE grpc_rb_status_details(VALUE self) {
  VALUE from_ruby;
  grpc_rb_status *wrapper = NULL;
  grpc_status *status;

  Data_Get_Struct(self, grpc_rb_status, wrapper);
  if (wrapper->mark != Qnil) {
    from_ruby = rb_ivar_get(wrapper->mark, id_details);
    if (from_ruby != Qnil) {
      return from_ruby;
    }
  }

  status = wrapper->wrapped;
  if (status == NULL || status->details == NULL) {
    return Qnil;
  }

  return rb_str_new2(status->details);
}

void Init_google_status_codes() {
  /* Constants representing the status codes or grpc_status_code in status.h */
  VALUE rb_mStatusCodes = rb_define_module_under(rb_mGoogleRpcCore,
                                                 "StatusCodes");
  rb_define_const(rb_mStatusCodes, "OK", INT2NUM(GRPC_STATUS_OK));
  rb_define_const(rb_mStatusCodes, "CANCELLED", INT2NUM(GRPC_STATUS_CANCELLED));
  rb_define_const(rb_mStatusCodes, "UNKNOWN", INT2NUM(GRPC_STATUS_UNKNOWN));
  rb_define_const(rb_mStatusCodes, "INVALID_ARGUMENT",
                  INT2NUM(GRPC_STATUS_INVALID_ARGUMENT));
  rb_define_const(rb_mStatusCodes, "DEADLINE_EXCEEDED",
                  INT2NUM(GRPC_STATUS_DEADLINE_EXCEEDED));
  rb_define_const(rb_mStatusCodes, "NOT_FOUND", INT2NUM(GRPC_STATUS_NOT_FOUND));
  rb_define_const(rb_mStatusCodes, "ALREADY_EXISTS",
                  INT2NUM(GRPC_STATUS_ALREADY_EXISTS));
  rb_define_const(rb_mStatusCodes, "PERMISSION_DENIED",
                  INT2NUM(GRPC_STATUS_PERMISSION_DENIED));
  rb_define_const(rb_mStatusCodes, "UNAUTHENTICATED",
                  INT2NUM(GRPC_STATUS_UNAUTHENTICATED));
  rb_define_const(rb_mStatusCodes, "RESOURCE_EXHAUSTED",
                  INT2NUM(GRPC_STATUS_RESOURCE_EXHAUSTED));
  rb_define_const(rb_mStatusCodes, "FAILED_PRECONDITION",
                  INT2NUM(GRPC_STATUS_FAILED_PRECONDITION));
  rb_define_const(rb_mStatusCodes, "ABORTED", INT2NUM(GRPC_STATUS_ABORTED));
  rb_define_const(rb_mStatusCodes, "OUT_OF_RANGE",
                  INT2NUM(GRPC_STATUS_OUT_OF_RANGE));
  rb_define_const(rb_mStatusCodes, "UNIMPLEMENTED",
                  INT2NUM(GRPC_STATUS_UNIMPLEMENTED));
  rb_define_const(rb_mStatusCodes, "INTERNAL", INT2NUM(GRPC_STATUS_INTERNAL));
  rb_define_const(rb_mStatusCodes, "UNAVAILABLE",
                  INT2NUM(GRPC_STATUS_UNAVAILABLE));
  rb_define_const(rb_mStatusCodes, "DATA_LOSS", INT2NUM(GRPC_STATUS_DATA_LOSS));
}

/* rb_cStatus is the Status class whose instances proxy grpc_status. */
VALUE rb_cStatus = Qnil;

/* Initializes the Status class. */
void Init_google_rpc_status() {
  rb_cStatus = rb_define_class_under(rb_mGoogleRpcCore, "Status", rb_cObject);

  /* Allocates an object whose memory is managed by the Ruby. */
  rb_define_alloc_func(rb_cStatus, grpc_rb_status_alloc);

  /* Provides a ruby constructor and support for dup/clone. */
  rb_define_method(rb_cStatus, "initialize", grpc_rb_status_init, 2);
  rb_define_method(rb_cStatus, "initialize_copy", grpc_rb_status_init_copy, 1);

  /* Provides accessors for the code and details. */
  rb_define_method(rb_cStatus, "code", grpc_rb_status_code, 0);
  rb_define_method(rb_cStatus, "details", grpc_rb_status_details, 0);
  id_details = rb_intern("__details");
  Init_google_status_codes();
}

VALUE grpc_rb_status_create_with_mark(VALUE mark, grpc_status* s) {
  grpc_rb_status *status = NULL;
  if (s == NULL) {
    return Qnil;
  }
  status = ALLOC(grpc_rb_status);
  status->wrapped = s;
  status->mark = mark;
  return Data_Wrap_Struct(rb_cStatus, grpc_rb_status_mark, grpc_rb_status_free,
                          status);
}

/* Gets the wrapped status from the ruby wrapper */
grpc_status* grpc_rb_get_wrapped_status(VALUE v) {
  grpc_rb_status *wrapper = NULL;
  Data_Get_Struct(v, grpc_rb_status, wrapper);
  return wrapper->wrapped;
}
