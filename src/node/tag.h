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

#include <node.h>

namespace grpc {
namespace node {

/* Create a void* tag that can be passed to various grpc_call functions from
   a javascript value and the javascript wrapper for the call. The call can be
   null. */
void *CreateTag(v8::Handle<v8::Value> tag, v8::Handle<v8::Value> call);
/* Return the javascript value stored in the tag */
v8::Handle<v8::Value> GetTagHandle(void *tag);
/* Returns true if the call was set (non-null) when the tag was created */
bool TagHasCall(void *tag);
/* Returns the javascript wrapper for the call associated with this tag */
v8::Handle<v8::Value> TagGetCall(void *call);
/* Destroy the tag and all resources it is holding. It is illegal to call any
   of these other functions on a tag after it has been destroyed. */
void DestroyTag(void *tag);

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_TAG_H_
