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

#ifndef GRPC_RB_CALL_H_
#define GRPC_RB_CALL_H_

#include <ruby/ruby.h>

#include <grpc/grpc.h>

/* Gets the wrapped call from a VALUE. */
grpc_call* grpc_rb_get_wrapped_call(VALUE v);

/* Gets the VALUE corresponding to given grpc_call. */
VALUE grpc_rb_wrap_call(grpc_call *c, grpc_completion_queue *q);

/* Provides the details of an call error */
const char* grpc_call_error_detail_of(grpc_call_error err);

/* Converts a metadata array to a hash. */
VALUE grpc_rb_md_ary_to_h(grpc_metadata_array *md_ary);

/* grpc_rb_md_ary_convert converts a ruby metadata hash into
   a grpc_metadata_array.
*/
void grpc_rb_md_ary_convert(VALUE md_ary_hash,
                            grpc_metadata_array *md_ary);

/* grpc_rb_eCallError is the ruby class of the exception thrown during call
   operations. */
extern VALUE grpc_rb_eCallError;

/* Initializes the Call class. */
void Init_grpc_call();

#endif /* GRPC_RB_CALL_H_ */
