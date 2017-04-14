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

#ifndef GRPC_RB_H_
#define GRPC_RB_H_

#include <sys/time.h>
#include <ruby/ruby.h>

#include <grpc/support/time.h>

/* grpc_rb_mGrpcCore is the module containing the ruby wrapper GRPC classes. */
extern VALUE grpc_rb_mGrpcCore;

/* grpc_rb_sNewServerRpc is the struct that holds new server rpc details. */
extern VALUE grpc_rb_sNewServerRpc;

/* grpc_rb_sStruct is the struct that holds status details. */
extern VALUE grpc_rb_sStatus;

/* sym_code is the symbol for the code attribute of grpc_rb_sStatus. */
extern VALUE sym_code;

/* sym_details is the symbol for the details attribute of grpc_rb_sStatus. */
extern VALUE sym_details;

/* sym_metadata is the symbol for the metadata attribute of grpc_rb_sStatus. */
extern VALUE sym_metadata;

/* GC_NOT_MARKED is used in calls to Data_Wrap_Struct to indicate that the
   wrapped struct does not need to participate in ruby gc. */
#define GRPC_RB_GC_NOT_MARKED (RUBY_DATA_FUNC)(NULL)

/* GC_DONT_FREED is used in calls to Data_Wrap_Struct to indicate that the
   wrapped struct should not be freed the wrapped ruby object is released by
   the garbage collector. */
#define GRPC_RB_GC_DONT_FREE (RUBY_DATA_FUNC)(NULL)

/* GRPC_RB_MEMSIZE_UNAVAILABLE is used in rb_data_type_t to indicate that the
 * number of bytes used by the wrapped struct is not available. */
#define GRPC_RB_MEMSIZE_UNAVAILABLE (size_t (*)(const void*))(NULL)

/* A ruby object alloc func that fails by raising an exception. */
VALUE grpc_rb_cannot_alloc(VALUE cls);

/* A ruby object init func that fails by raising an exception. */
VALUE grpc_rb_cannot_init(VALUE self);

/* A ruby object clone init func that fails by raising an exception. */
VALUE grpc_rb_cannot_init_copy(VALUE copy, VALUE self);

/* grpc_rb_time_timeval creates a gpr_timespec from a ruby time object. */
gpr_timespec grpc_rb_time_timeval(VALUE time, int interval);

void grpc_ruby_once_init();

#endif /* GRPC_RB_H_ */
