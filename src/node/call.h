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

#ifndef NET_GRPC_NODE_CALL_H_
#define NET_GRPC_NODE_CALL_H_

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"

#include "channel.h"

namespace grpc {
namespace node {

/* Wrapper class for grpc_call structs. */
class Call : public ::node::ObjectWrap {
 public:
  static void Init(v8::Handle<v8::Object> exports);
  static bool HasInstance(v8::Handle<v8::Value> val);
  /* Wrap a grpc_call struct in a javascript object */
  static v8::Handle<v8::Value> WrapStruct(grpc_call *call);

 private:
  explicit Call(grpc_call *call);
  ~Call();

  // Prevent copying
  Call(const Call &);
  Call &operator=(const Call &);

  static NAN_METHOD(New);
  static NAN_METHOD(AddMetadata);
  static NAN_METHOD(Invoke);
  static NAN_METHOD(ServerAccept);
  static NAN_METHOD(ServerEndInitialMetadata);
  static NAN_METHOD(Cancel);
  static NAN_METHOD(StartWrite);
  static NAN_METHOD(StartWriteStatus);
  static NAN_METHOD(WritesDone);
  static NAN_METHOD(StartRead);
  static v8::Persistent<v8::Function> constructor;
  // Used for typechecking instances of this javascript class
  static v8::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_call *wrapped_call;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_CALL_H_
