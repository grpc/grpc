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

#include <memory>
#include <vector>
#include <map>

#include <node.h>

#include "grpc/support/log.h"
#include "grpc/grpc.h"
#include "grpc/support/alloc.h"
#include "grpc/support/time.h"
#include "byte_buffer.h"
#include "call.h"
#include "channel.h"
#include "completion_queue_async_worker.h"
#include "timeval.h"

using std::unique_ptr;
using std::shared_ptr;
using std::vector;

namespace grpc {
namespace node {

using v8::Array;
using v8::Boolean;
using v8::Exception;
using v8::External;
using v8::Function;
using v8::FunctionTemplate;
using v8::Handle;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::Persistent;
using v8::Uint32;
using v8::String;
using v8::Value;

NanCallback *Call::constructor;
Persistent<FunctionTemplate> Call::fun_tpl;

bool EndsWith(const char *str, const char *substr) {
  return strcmp(str+strlen(str)-strlen(substr), substr) == 0;
}

bool CreateMetadataArray(Handle<Object> metadata, grpc_metadata_array *array,
                         shared_ptr<Resources> resources) {
  NanScope();
  grpc_metadata_array_init(array);
  Handle<Array> keys(metadata->GetOwnPropertyNames());
  for (unsigned int i = 0; i < keys->Length(); i++) {
    Handle<String> current_key(keys->Get(i)->ToString());
    if (!metadata->Get(current_key)->IsArray()) {
      return false;
    }
    array->capacity += Local<Array>::Cast(metadata->Get(current_key))->Length();
  }
  array->metadata = reinterpret_cast<grpc_metadata*>(
      gpr_malloc(array->capacity * sizeof(grpc_metadata)));
  for (unsigned int i = 0; i < keys->Length(); i++) {
    Handle<String> current_key(keys->Get(i)->ToString());
    NanUtf8String *utf8_key = new NanUtf8String(current_key);
    resources->strings.push_back(unique_ptr<NanUtf8String>(utf8_key));
    Handle<Array> values = Local<Array>::Cast(metadata->Get(current_key));
    for (unsigned int j = 0; j < values->Length(); j++) {
      Handle<Value> value = values->Get(j);
      grpc_metadata *current = &array->metadata[array->count];
      current->key = **utf8_key;
      // Only allow binary headers for "-bin" keys
      if (EndsWith(current->key, "-bin")) {
        if (::node::Buffer::HasInstance(value)) {
          current->value = ::node::Buffer::Data(value);
          current->value_length = ::node::Buffer::Length(value);
          Persistent<Value> *handle = new Persistent<Value>();
          NanAssignPersistent(*handle, value);
          resources->handles.push_back(unique_ptr<PersistentHolder>(
              new PersistentHolder(handle)));
          continue;
        }
      }
      if (value->IsString()) {
        Handle<String> string_value = value->ToString();
        NanUtf8String *utf8_value = new NanUtf8String(string_value);
        resources->strings.push_back(unique_ptr<NanUtf8String>(utf8_value));
        current->value = **utf8_value;
        current->value_length = string_value->Length();
      } else {
        return false;
      }
      array->count += 1;
    }
  }
  return true;
}

Handle<Value> ParseMetadata(const grpc_metadata_array *metadata_array) {
  NanEscapableScope();
  grpc_metadata *metadata_elements = metadata_array->metadata;
  size_t length = metadata_array->count;
  std::map<const char*, size_t> size_map;
  std::map<const char*, size_t> index_map;

  for (unsigned int i = 0; i < length; i++) {
    const char *key = metadata_elements[i].key;
    if (size_map.count(key)) {
      size_map[key] += 1;
    }
    index_map[key] = 0;
  }
  Handle<Object> metadata_object = NanNew<Object>();
  for (unsigned int i = 0; i < length; i++) {
    grpc_metadata* elem = &metadata_elements[i];
    Handle<String> key_string = NanNew(elem->key);
    Handle<Array> array;
    if (metadata_object->Has(key_string)) {
      array = Handle<Array>::Cast(metadata_object->Get(key_string));
    } else {
      array = NanNew<Array>(size_map[elem->key]);
      metadata_object->Set(key_string, array);
    }
    if (EndsWith(elem->key, "-bin")) {
      array->Set(index_map[elem->key],
                 MakeFastBuffer(
                     NanNewBufferHandle(elem->value, elem->value_length)));
    } else {
      array->Set(index_map[elem->key], NanNew(elem->value));
    }
    index_map[elem->key] += 1;
  }
  return NanEscapeScope(metadata_object);
}

Handle<Value> Op::GetOpType() const {
  NanEscapableScope();
  return NanEscapeScope(NanNew<String>(GetTypeString()));
}

class SendMetadataOp : public Op {
 public:
  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    return NanEscapeScope(NanTrue());
  }
  bool ParseOp(Handle<Value> value, grpc_op *out,
               shared_ptr<Resources> resources) {
    if (!value->IsObject()) {
      return false;
    }
    grpc_metadata_array array;
    if (!CreateMetadataArray(value->ToObject(), &array, resources)) {
      return false;
    }
    out->data.send_initial_metadata.count = array.count;
    out->data.send_initial_metadata.metadata = array.metadata;
    return true;
  }
 protected:
  std::string GetTypeString() const {
    return "send_metadata";
  }
};

class SendMessageOp : public Op {
 public:
  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    return NanEscapeScope(NanTrue());
  }
  bool ParseOp(Handle<Value> value, grpc_op *out,
               shared_ptr<Resources> resources) {
    if (!::node::Buffer::HasInstance(value)) {
      return false;
    }
    out->data.send_message = BufferToByteBuffer(value);
    Persistent<Value> *handle = new Persistent<Value>();
    NanAssignPersistent(*handle, value);
    resources->handles.push_back(unique_ptr<PersistentHolder>(
        new PersistentHolder(handle)));
    return true;
  }
 protected:
  std::string GetTypeString() const {
    return "send_message";
  }
};

class SendClientCloseOp : public Op {
 public:
  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    return NanEscapeScope(NanTrue());
  }
  bool ParseOp(Handle<Value> value, grpc_op *out,
               shared_ptr<Resources> resources) {
    return true;
  }
 protected:
  std::string GetTypeString() const {
    return "client_close";
  }
};

class SendServerStatusOp : public Op {
 public:
  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    return NanEscapeScope(NanTrue());
  }
  bool ParseOp(Handle<Value> value, grpc_op *out,
               shared_ptr<Resources> resources) {
    if (!value->IsObject()) {
      return false;
    }
    Handle<Object> server_status = value->ToObject();
    if (!server_status->Get(NanNew("metadata"))->IsObject()) {
      return false;
    }
    if (!server_status->Get(NanNew("code"))->IsUint32()) {
      return false;
    }
    if (!server_status->Get(NanNew("details"))->IsString()) {
      return false;
    }
    grpc_metadata_array array;
    if (!CreateMetadataArray(server_status->Get(NanNew("metadata"))->
                             ToObject(),
                             &array, resources)) {
      return false;
    }
    out->data.send_status_from_server.trailing_metadata_count = array.count;
    out->data.send_status_from_server.trailing_metadata = array.metadata;
    out->data.send_status_from_server.status =
        static_cast<grpc_status_code>(
            server_status->Get(NanNew("code"))->Uint32Value());
    NanUtf8String *str = new NanUtf8String(
        server_status->Get(NanNew("details")));
    resources->strings.push_back(unique_ptr<NanUtf8String>(str));
    out->data.send_status_from_server.status_details = **str;
    return true;
  }
 protected:
  std::string GetTypeString() const {
    return "send_status";
  }
};

class GetMetadataOp : public Op {
 public:
  GetMetadataOp() {
    grpc_metadata_array_init(&recv_metadata);
  }

  ~GetMetadataOp() {
    grpc_metadata_array_destroy(&recv_metadata);
  }

  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    return NanEscapeScope(ParseMetadata(&recv_metadata));
  }

  bool ParseOp(Handle<Value> value, grpc_op *out,
               shared_ptr<Resources> resources) {
    out->data.recv_initial_metadata = &recv_metadata;
    return true;
  }

 protected:
  std::string GetTypeString() const {
    return "metadata";
  }

 private:
  grpc_metadata_array recv_metadata;
};

class ReadMessageOp : public Op {
 public:
  ReadMessageOp() {
    recv_message = NULL;
  }
  ~ReadMessageOp() {
    if (recv_message != NULL) {
      gpr_free(recv_message);
    }
  }
  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    return NanEscapeScope(ByteBufferToBuffer(recv_message));
  }

  bool ParseOp(Handle<Value> value, grpc_op *out,
               shared_ptr<Resources> resources) {
    out->data.recv_message = &recv_message;
    return true;
  }

 protected:
  std::string GetTypeString() const {
    return "read";
  }

 private:
  grpc_byte_buffer *recv_message;
};

class ClientStatusOp : public Op {
 public:
  ClientStatusOp() {
    grpc_metadata_array_init(&metadata_array);
    status_details = NULL;
    details_capacity = 0;
  }

  ~ClientStatusOp() {
    grpc_metadata_array_destroy(&metadata_array);
    gpr_free(status_details);
  }

  bool ParseOp(Handle<Value> value, grpc_op *out,
               shared_ptr<Resources> resources) {
    out->data.recv_status_on_client.trailing_metadata = &metadata_array;
    out->data.recv_status_on_client.status = &status;
    out->data.recv_status_on_client.status_details = &status_details;
    out->data.recv_status_on_client.status_details_capacity = &details_capacity;
    return true;
  }

  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    Handle<Object> status_obj = NanNew<Object>();
    status_obj->Set(NanNew("code"), NanNew<Number>(status));
    if (status_details != NULL) {
      status_obj->Set(NanNew("details"), NanNew(status_details));
    }
    status_obj->Set(NanNew("metadata"), ParseMetadata(&metadata_array));
    return NanEscapeScope(status_obj);
  }
 protected:
  std::string GetTypeString() const {
    return "status";
  }
 private:
  grpc_metadata_array metadata_array;
  grpc_status_code status;
  char *status_details;
  size_t details_capacity;
};

class ServerCloseResponseOp : public Op {
 public:
  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    return NanEscapeScope(NanNew<Boolean>(cancelled));
  }

  bool ParseOp(Handle<Value> value, grpc_op *out,
               shared_ptr<Resources> resources) {
    out->data.recv_close_on_server.cancelled = &cancelled;
    return true;
  }

 protected:
  std::string GetTypeString() const {
    return "cancelled";
  }

 private:
  int cancelled;
};

tag::tag(NanCallback *callback, OpVec *ops,
         shared_ptr<Resources> resources) :
    callback(callback), ops(ops), resources(resources){
}

tag::~tag() {
  delete callback;
  delete ops;
}

Handle<Value> GetTagNodeValue(void *tag) {
  NanEscapableScope();
  struct tag *tag_struct = reinterpret_cast<struct tag *>(tag);
  Handle<Object> tag_obj = NanNew<Object>();
  for (vector<unique_ptr<Op> >::iterator it = tag_struct->ops->begin();
       it != tag_struct->ops->end(); ++it) {
    Op *op_ptr = it->get();
    tag_obj->Set(op_ptr->GetOpType(), op_ptr->GetNodeValue());
  }
  return NanEscapeScope(tag_obj);
}

NanCallback *GetTagCallback(void *tag) {
  struct tag *tag_struct = reinterpret_cast<struct tag *>(tag);
  return tag_struct->callback;
}

void DestroyTag(void *tag) {
  struct tag *tag_struct = reinterpret_cast<struct tag *>(tag);
  delete tag_struct;
}

Call::Call(grpc_call *call) : wrapped_call(call) {
}

Call::~Call() {
  grpc_call_destroy(wrapped_call);
}

void Call::Init(Handle<Object> exports) {
  NanScope();
  Local<FunctionTemplate> tpl = NanNew<FunctionTemplate>(New);
  tpl->SetClassName(NanNew("Call"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  NanSetPrototypeTemplate(tpl, "startBatch",
                          NanNew<FunctionTemplate>(StartBatch)->GetFunction());
  NanSetPrototypeTemplate(tpl, "cancel",
                          NanNew<FunctionTemplate>(Cancel)->GetFunction());
  NanAssignPersistent(fun_tpl, tpl);
  Handle<Function> ctr = tpl->GetFunction();
  ctr->Set(NanNew("WRITE_BUFFER_HINT"),
           NanNew<Uint32, uint32_t>(GRPC_WRITE_BUFFER_HINT));
  ctr->Set(NanNew("WRITE_NO_COMPRESS"),
           NanNew<Uint32, uint32_t>(GRPC_WRITE_NO_COMPRESS));
  exports->Set(NanNew("Call"), ctr);
  constructor = new NanCallback(ctr);
}

bool Call::HasInstance(Handle<Value> val) {
  NanScope();
  return NanHasInstance(fun_tpl, val);
}

Handle<Value> Call::WrapStruct(grpc_call *call) {
  NanEscapableScope();
  if (call == NULL) {
    return NanEscapeScope(NanNull());
  }
  const int argc = 1;
  Handle<Value> argv[argc] = {NanNew<External>(reinterpret_cast<void *>(call))};
  return NanEscapeScope(constructor->GetFunction()->NewInstance(argc, argv));
}

NAN_METHOD(Call::New) {
  NanScope();

  if (args.IsConstructCall()) {
    Call *call;
    if (args[0]->IsExternal()) {
      Handle<External> ext = args[0].As<External>();
      // This option is used for wrapping an existing call
      grpc_call *call_value =
          reinterpret_cast<grpc_call *>(ext->Value());
      call = new Call(call_value);
    } else {
      if (!Channel::HasInstance(args[0])) {
        return NanThrowTypeError("Call's first argument must be a Channel");
      }
      if (!args[1]->IsString()) {
        return NanThrowTypeError("Call's second argument must be a string");
      }
      if (!(args[2]->IsNumber() || args[2]->IsDate())) {
        return NanThrowTypeError(
            "Call's third argument must be a date or a number");
      }
      Handle<Object> channel_object = args[0]->ToObject();
      Channel *channel = ObjectWrap::Unwrap<Channel>(channel_object);
      if (channel->GetWrappedChannel() == NULL) {
        return NanThrowError("Call cannot be created from a closed channel");
      }
      NanUtf8String method(args[1]);
      double deadline = args[2]->NumberValue();
      grpc_channel *wrapped_channel = channel->GetWrappedChannel();
      grpc_call *wrapped_call = grpc_channel_create_call(
          wrapped_channel, CompletionQueueAsyncWorker::GetQueue(), *method,
          channel->GetHost(), MillisecondsToTimespec(deadline));
      call = new Call(wrapped_call);
      args.This()->SetHiddenValue(NanNew("channel_"), channel_object);
    }
    call->Wrap(args.This());
    NanReturnValue(args.This());
  } else {
    const int argc = 4;
    Local<Value> argv[argc] = {args[0], args[1], args[2], args[3]};
    NanReturnValue(constructor->GetFunction()->NewInstance(argc, argv));
  }
}

NAN_METHOD(Call::StartBatch) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("startBatch can only be called on Call objects");
  }
  if (!args[0]->IsObject()) {
    return NanThrowError("startBatch's first argument must be an object");
  }
  if (!args[1]->IsFunction()) {
    return NanThrowError("startBatch's second argument must be a callback");
  }
  Handle<Function> callback_func = args[1].As<Function>();
  Call *call = ObjectWrap::Unwrap<Call>(args.This());
  shared_ptr<Resources> resources(new Resources);
  Handle<Object> obj = args[0]->ToObject();
  Handle<Array> keys = obj->GetOwnPropertyNames();
  size_t nops = keys->Length();
  vector<grpc_op> ops(nops);
  unique_ptr<OpVec> op_vector(new OpVec());
  for (unsigned int i = 0; i < nops; i++) {
    unique_ptr<Op> op;
    if (!keys->Get(i)->IsUint32()) {
      return NanThrowError(
          "startBatch's first argument's keys must be integers");
    }
    uint32_t type = keys->Get(i)->Uint32Value();
    ops[i].op = static_cast<grpc_op_type>(type);
    ops[i].flags = 0;
    switch (type) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        op.reset(new SendMetadataOp());
        break;
      case GRPC_OP_SEND_MESSAGE:
        op.reset(new SendMessageOp());
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        op.reset(new SendClientCloseOp());
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        op.reset(new SendServerStatusOp());
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        op.reset(new GetMetadataOp());
        break;
      case GRPC_OP_RECV_MESSAGE:
        op.reset(new ReadMessageOp());
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        op.reset(new ClientStatusOp());
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        op.reset(new ServerCloseResponseOp());
        break;
      default:
        return NanThrowError("Argument object had an unrecognized key");
    }
    if (!op->ParseOp(obj->Get(type), &ops[i], resources)) {
      return NanThrowTypeError("Incorrectly typed arguments to startBatch");
    }
    op_vector->push_back(std::move(op));
  }
  NanCallback *callback = new NanCallback(callback_func);
  grpc_call_error error = grpc_call_start_batch(
      call->wrapped_call, &ops[0], nops, new struct tag(
          callback, op_vector.release(), resources));
  if (error != GRPC_CALL_OK) {
    return NanThrowError("startBatch failed", error);
  }
  CompletionQueueAsyncWorker::Next();
  NanReturnUndefined();
}

NAN_METHOD(Call::Cancel) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("cancel can only be called on Call objects");
  }
  Call *call = ObjectWrap::Unwrap<Call>(args.This());
  grpc_call_error error = grpc_call_cancel(call->wrapped_call);
  if (error != GRPC_CALL_OK) {
    return NanThrowError("cancel failed", error);
  }
  NanReturnUndefined();
}

}  // namespace node
}  // namespace grpc
