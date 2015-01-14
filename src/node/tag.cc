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

#include <stdlib.h>
#include <node.h>
#include <nan.h>
#include "tag.h"

namespace grpc {
namespace node {

using v8::Handle;
using v8::HandleScope;
using v8::Persistent;
using v8::Value;

struct tag {
  tag(Persistent<Value> *tag, Persistent<Value> *call)
      : persist_tag(tag), persist_call(call) {}

  ~tag() {
    persist_tag->Dispose();
    if (persist_call != NULL) {
      persist_call->Dispose();
    }
  }
  Persistent<Value> *persist_tag;
  Persistent<Value> *persist_call;
};

void *CreateTag(Handle<Value> tag, Handle<Value> call) {
  NanScope();
  Persistent<Value> *persist_tag = new Persistent<Value>();
  NanAssignPersistent(*persist_tag, tag);
  Persistent<Value> *persist_call;
  if (call->IsNull() || call->IsUndefined()) {
    persist_call = NULL;
  } else {
    persist_call = new Persistent<Value>();
    NanAssignPersistent(*persist_call, call);
  }
  struct tag *tag_struct = new struct tag(persist_tag, persist_call);
  return reinterpret_cast<void *>(tag_struct);
}

Handle<Value> GetTagHandle(void *tag) {
  NanEscapableScope();
  struct tag *tag_struct = reinterpret_cast<struct tag *>(tag);
  Handle<Value> tag_value = NanNew<Value>(*tag_struct->persist_tag);
  return NanEscapeScope(tag_value);
}

bool TagHasCall(void *tag) {
  struct tag *tag_struct = reinterpret_cast<struct tag *>(tag);
  return tag_struct->persist_call != NULL;
}

Handle<Value> TagGetCall(void *tag) {
  NanEscapableScope();
  struct tag *tag_struct = reinterpret_cast<struct tag *>(tag);
  if (tag_struct->persist_call == NULL) {
    return NanEscapeScope(NanNull());
  }
  Handle<Value> call_value = NanNew<Value>(*tag_struct->persist_call);
  return NanEscapeScope(call_value);
}

void DestroyTag(void *tag) { delete reinterpret_cast<struct tag *>(tag); }

}  // namespace node
}  // namespace grpc
