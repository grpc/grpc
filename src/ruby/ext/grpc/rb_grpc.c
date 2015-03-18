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

#include "rb_grpc.h"

#include <math.h>
#include <ruby.h>
#include <sys/time.h>

#include <grpc/grpc.h>
#include <grpc/support/time.h>
#include "rb_byte_buffer.h"
#include "rb_call.h"
#include "rb_channel.h"
#include "rb_completion_queue.h"
#include "rb_event.h"
#include "rb_metadata.h"
#include "rb_server.h"
#include "rb_credentials.h"
#include "rb_server_credentials.h"

/* Define common vars and funcs declared in rb.h */
const RUBY_DATA_FUNC GC_NOT_MARKED = NULL;
const RUBY_DATA_FUNC GC_DONT_FREE = NULL;

VALUE rb_cTimeVal = Qnil;

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
  rb_raise(rb_eTypeError,
           "initialization of %s only allowed from the gRPC native layer",
           rb_obj_classname(copy));
  return Qnil;
}

/* id_tv_{,u}sec are accessor methods on Ruby Time instances. */
static ID id_tv_sec;
static ID id_tv_nsec;

/**
 * grpc_rb_time_timeval creates a time_eval from a ruby time object.
 *
 * This func is copied from ruby source, MRI/source/time.c, which is published
 * under the same license as the ruby.h, on which the entire extensions is
 * based.
 */
gpr_timespec grpc_rb_time_timeval(VALUE time, int interval) {
  gpr_timespec t;
  gpr_timespec *time_const;
  const char *tstr = interval ? "time interval" : "time";
  const char *want = " want <secs from epoch>|<Time>|<GRPC::TimeConst.*>";

  switch (TYPE(time)) {
    case T_DATA:
      if (CLASS_OF(time) == rb_cTimeVal) {
        Data_Get_Struct(time, gpr_timespec, time_const);
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
        t.tv_sec = (time_t)f;
        if (f != t.tv_sec) {
          rb_raise(rb_eRangeError, "%f out of Time range",
                   RFLOAT_VALUE(time));
        }
        t.tv_nsec = (time_t)(d * 1e9 + 0.5);
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

void Init_grpc_status_codes() {
  /* Constants representing the status codes or grpc_status_code in status.h */
  VALUE rb_mStatusCodes =
      rb_define_module_under(rb_mGrpcCore, "StatusCodes");
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

/* id_at is the constructor method of the ruby standard Time class. */
static ID id_at;

/* id_inspect is the inspect method found on various ruby objects. */
static ID id_inspect;

/* id_to_s is the to_s method found on various ruby objects. */
static ID id_to_s;

/* Converts `a wrapped time constant to a standard time. */
VALUE grpc_rb_time_val_to_time(VALUE self) {
  gpr_timespec *time_const = NULL;
  Data_Get_Struct(self, gpr_timespec, time_const);
  return rb_funcall(rb_cTime, id_at, 2, INT2NUM(time_const->tv_sec),
                    INT2NUM(time_const->tv_nsec));
}

/* Invokes inspect on the ctime version of the time val. */
VALUE grpc_rb_time_val_inspect(VALUE self) {
  return rb_funcall(grpc_rb_time_val_to_time(self), id_inspect, 0);
}

/* Invokes to_s on the ctime version of the time val. */
VALUE grpc_rb_time_val_to_s(VALUE self) {
  return rb_funcall(grpc_rb_time_val_to_time(self), id_to_s, 0);
}

/* Adds a module with constants that map to gpr's static timeval structs. */
void Init_grpc_time_consts() {
  VALUE rb_mTimeConsts =
      rb_define_module_under(rb_mGrpcCore, "TimeConsts");
  rb_cTimeVal =
      rb_define_class_under(rb_mGrpcCore, "TimeSpec", rb_cObject);
  rb_define_const(rb_mTimeConsts, "ZERO",
                  Data_Wrap_Struct(rb_cTimeVal, GC_NOT_MARKED, GC_DONT_FREE,
                                   (void *)&gpr_time_0));
  rb_define_const(rb_mTimeConsts, "INFINITE_FUTURE",
                  Data_Wrap_Struct(rb_cTimeVal, GC_NOT_MARKED, GC_DONT_FREE,
                                   (void *)&gpr_inf_future));
  rb_define_const(rb_mTimeConsts, "INFINITE_PAST",
                  Data_Wrap_Struct(rb_cTimeVal, GC_NOT_MARKED, GC_DONT_FREE,
                                   (void *)&gpr_inf_past));
  rb_define_method(rb_cTimeVal, "to_time", grpc_rb_time_val_to_time, 0);
  rb_define_method(rb_cTimeVal, "inspect", grpc_rb_time_val_inspect, 0);
  rb_define_method(rb_cTimeVal, "to_s", grpc_rb_time_val_to_s, 0);
  id_at = rb_intern("at");
  id_inspect = rb_intern("inspect");
  id_to_s = rb_intern("to_s");
  id_tv_sec = rb_intern("tv_sec");
  id_tv_nsec = rb_intern("tv_nsec");
}

void grpc_rb_shutdown(void *vm) { grpc_shutdown(); }

/* Initialize the GRPC module structs */

/* rb_sNewServerRpc is the struct that holds new server rpc details. */
VALUE rb_sNewServerRpc = Qnil;
/* rb_sStatus is the struct that holds status details. */
VALUE rb_sStatus = Qnil;

/* Initialize the GRPC module. */
VALUE rb_mGRPC = Qnil;
VALUE rb_mGrpcCore = Qnil;

void Init_grpc() {
  grpc_init();
  ruby_vm_at_exit(grpc_rb_shutdown);
  rb_mGRPC = rb_define_module("GRPC");
  rb_mGrpcCore = rb_define_module_under(rb_mGRPC, "Core");
  rb_sNewServerRpc = rb_struct_define("NewServerRpc", "method", "host",
                                      "deadline", "metadata", NULL);
  rb_sStatus = rb_struct_define("Status", "code", "details", "metadata", NULL);

  Init_grpc_byte_buffer();
  Init_grpc_event();
  Init_grpc_channel();
  Init_grpc_completion_queue();
  Init_grpc_call();
  Init_grpc_credentials();
  Init_grpc_metadata();
  Init_grpc_server();
  Init_grpc_server_credentials();
  Init_grpc_status_codes();
  Init_grpc_time_consts();
}
