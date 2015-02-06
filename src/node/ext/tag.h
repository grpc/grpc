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

#ifndef NET_GRPC_NODE_TAG_H_
#define NET_GRPC_NODE_TAG_H_

#include <grpc/grpc.h>
#include <node.h>
#include <nan.h>

namespace grpc {
namespace node {

/* Create a void* tag that can be passed to grpc_call_start_batch from a callback
   function and an ops array */
void *CreateTag(v8::Handle<v8::Function> callback, grpc_op *ops, size_t nops);

/* Create a void* tag that can be passed to grpc_server_request_call from a
   callback and the various out parameters to that function */
void *CreateTag(v8::Handle<v8::Function> callback, grpc_call **call,
                grpc_call_details *details,
                grpc_metadata_array *request_metadata);

/* Get the callback from the tag */
NanCallback GetCallback(void *tag);

/* Get the combined output value from the tag */
v8::Handle<v8::Value> GetNodevalue(void *tag);

/* Destroy the tag and all resources it is holding. It is illegal to call any
   of these other functions on a tag after it has been destroyed. */
void DestroyTag(void *tag);

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_TAG_H_
