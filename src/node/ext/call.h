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

#ifndef NET_GRPC_NODE_CALL_H_
#define NET_GRPC_NODE_CALL_H_

#include <memory>
#include <vector>

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"
#include "grpc/support/log.h"

#include "channel.h"


namespace grpc {
namespace node {

using std::unique_ptr;
using std::shared_ptr;

v8::Local<v8::Value> nanErrorWithCode(const char *msg, grpc_call_error code);

v8::Local<v8::Value> ParseMetadata(const grpc_metadata_array *metadata_array);

bool CreateMetadataArray(v8::Local<v8::Object> metadata,
                         grpc_metadata_array *array);

void DestroyMetadataArray(grpc_metadata_array *array);

/* Wrapper class for grpc_call structs. */
class Call : public Nan::ObjectWrap {
 public:
  static void Init(v8::Local<v8::Object> exports);
  static bool HasInstance(v8::Local<v8::Value> val);
  /* Wrap a grpc_call struct in a javascript object */
  static v8::Local<v8::Value> WrapStruct(grpc_call *call);

  void CompleteBatch(bool is_final_op);

 private:
  explicit Call(grpc_call *call);
  ~Call();

  // Prevent copying
  Call(const Call &);
  Call &operator=(const Call &);

  void DestroyCall();

  static NAN_METHOD(New);
  static NAN_METHOD(StartBatch);
  static NAN_METHOD(Cancel);
  static NAN_METHOD(CancelWithStatus);
  static NAN_METHOD(GetPeer);
  static NAN_METHOD(SetCredentials);
  static Nan::Callback *constructor;
  // Used for typechecking instances of this javascript class
  static Nan::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_call *wrapped_call;
  // The number of ops that were started but not completed on this call
  int pending_batches;
  /* Indicates whether the "final" op on a call has completed. For a client
     call, this is GRPC_OP_RECV_STATUS_ON_CLIENT and for a server call, this
     is GRPC_OP_SEND_STATUS_FROM_SERVER */
  bool has_final_op_completed;
};

class Op {
 public:
  virtual v8::Local<v8::Value> GetNodeValue() const = 0;
  virtual bool ParseOp(v8::Local<v8::Value> value, grpc_op *out) = 0;
  virtual ~Op();
  v8::Local<v8::Value> GetOpType() const;
  virtual bool IsFinalOp() = 0;
  virtual void OnComplete(bool success) = 0;

 protected:
  virtual std::string GetTypeString() const = 0;
};

typedef std::vector<unique_ptr<Op>> OpVec;
struct tag {
  tag(Nan::Callback *callback, OpVec *ops, Call *call,
      v8::Local<v8::Value> call_value);
  ~tag();
  Nan::Callback *callback;
  OpVec *ops;
  Call *call;
  Nan::Persistent<v8::Value, Nan::CopyablePersistentTraits<v8::Value>>
      call_persist;
};

void DestroyTag(void *tag);

void CompleteTag(void *tag, const char *error_message);

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_CALL_H_
