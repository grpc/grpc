/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef NET_GRPC_NODE_CALL_H_
#define NET_GRPC_NODE_CALL_H_

#include <memory>
#include <vector>

#include <nan.h>
#include <node.h>
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
  char *peer;
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
