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

#include <map>
#include <memory>
#include <vector>

#include <node.h>

#include "byte_buffer.h"
#include "call.h"
#include "call_credentials.h"
#include "channel.h"
#include "completion_queue.h"
#include "grpc/grpc.h"
#include "grpc/grpc_security.h"
#include "grpc/support/alloc.h"
#include "grpc/support/log.h"
#include "grpc/support/time.h"
#include "slice.h"
#include "timeval.h"

using std::unique_ptr;
using std::shared_ptr;
using std::vector;

namespace grpc {
namespace node {

using Nan::Callback;
using Nan::EscapableHandleScope;
using Nan::HandleScope;
using Nan::Maybe;
using Nan::MaybeLocal;
using Nan::ObjectWrap;
using Nan::Persistent;
using Nan::Utf8String;

using v8::Array;
using v8::Boolean;
using v8::Exception;
using v8::External;
using v8::Function;
using v8::FunctionTemplate;
using v8::Integer;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::Uint32;
using v8::String;
using v8::Value;

Callback *Call::constructor;
Persistent<FunctionTemplate> Call::fun_tpl;

/**
 * Helper function for throwing errors with a grpc_call_error value.
 * Modified from the answer by Gus Goose to
 * http://stackoverflow.com/questions/31794200.
 */
Local<Value> nanErrorWithCode(const char *msg, grpc_call_error code) {
  EscapableHandleScope scope;
  Local<Object> err = Nan::Error(msg).As<Object>();
  Nan::Set(err, Nan::New("code").ToLocalChecked(), Nan::New<Uint32>(code));
  return scope.Escape(err);
}

bool CreateMetadataArray(Local<Object> metadata, grpc_metadata_array *array) {
  HandleScope scope;
  Local<Array> keys = Nan::GetOwnPropertyNames(metadata).ToLocalChecked();
  for (unsigned int i = 0; i < keys->Length(); i++) {
    Local<String> current_key =
        Nan::To<String>(Nan::Get(keys, i).ToLocalChecked()).ToLocalChecked();
    Local<Value> value_array = Nan::Get(metadata, current_key).ToLocalChecked();
    if (!value_array->IsArray()) {
      return false;
    }
    array->capacity += Local<Array>::Cast(value_array)->Length();
  }
  array->metadata = reinterpret_cast<grpc_metadata *>(
      gpr_zalloc(array->capacity * sizeof(grpc_metadata)));
  for (unsigned int i = 0; i < keys->Length(); i++) {
    Local<String> current_key(Nan::To<String>(keys->Get(i)).ToLocalChecked());
    Local<Array> values =
        Local<Array>::Cast(Nan::Get(metadata, current_key).ToLocalChecked());
    grpc_slice key_slice = CreateSliceFromString(current_key);
    grpc_slice key_intern_slice = grpc_slice_intern(key_slice);
    grpc_slice_unref(key_slice);
    for (unsigned int j = 0; j < values->Length(); j++) {
      Local<Value> value = Nan::Get(values, j).ToLocalChecked();
      grpc_metadata *current = &array->metadata[array->count];
      current->key = key_intern_slice;
      // Only allow binary headers for "-bin" keys
      if (grpc_is_binary_header(key_intern_slice)) {
        if (::node::Buffer::HasInstance(value)) {
          current->value = CreateSliceFromBuffer(value);
        } else {
          return false;
        }
      } else {
        if (value->IsString()) {
          Local<String> string_value = Nan::To<String>(value).ToLocalChecked();
          current->value = CreateSliceFromString(string_value);
        } else {
          return false;
        }
      }
      array->count += 1;
    }
  }
  return true;
}

void DestroyMetadataArray(grpc_metadata_array *array) {
  for (size_t i = 0; i < array->count; i++) {
    // Don't unref keys because they are interned
    grpc_slice_unref(array->metadata[i].value);
  }
  grpc_metadata_array_destroy(array);
}

Local<Value> ParseMetadata(const grpc_metadata_array *metadata_array) {
  EscapableHandleScope scope;
  grpc_metadata *metadata_elements = metadata_array->metadata;
  size_t length = metadata_array->count;
  Local<Object> metadata_object = Nan::New<Object>();
  for (unsigned int i = 0; i < length; i++) {
    grpc_metadata *elem = &metadata_elements[i];
    // TODO(murgatroid99): Use zero-copy string construction instead
    Local<String> key_string = CopyStringFromSlice(elem->key);
    Local<Array> array;
    MaybeLocal<Value> maybe_array = Nan::Get(metadata_object, key_string);
    if (maybe_array.IsEmpty() || !maybe_array.ToLocalChecked()->IsArray()) {
      array = Nan::New<Array>(0);
      Nan::Set(metadata_object, key_string, array);
    } else {
      array = Local<Array>::Cast(maybe_array.ToLocalChecked());
    }
    if (grpc_is_binary_header(elem->key)) {
      Nan::Set(array, array->Length(), CreateBufferFromSlice(elem->value));
    } else {
      // TODO(murgatroid99): Use zero-copy string construction instead
      Nan::Set(array, array->Length(), CopyStringFromSlice(elem->value));
    }
  }
  return scope.Escape(metadata_object);
}

Local<Value> Op::GetOpType() const {
  EscapableHandleScope scope;
  return scope.Escape(Nan::New(GetTypeString()).ToLocalChecked());
}

Op::~Op() {}

class SendMetadataOp : public Op {
 public:
  SendMetadataOp() { grpc_metadata_array_init(&send_metadata); }
  ~SendMetadataOp() { DestroyMetadataArray(&send_metadata); }
  Local<Value> GetNodeValue() const {
    EscapableHandleScope scope;
    return scope.Escape(Nan::True());
  }
  bool ParseOp(Local<Value> value, grpc_op *out) {
    if (!value->IsObject()) {
      return false;
    }
    MaybeLocal<Object> maybe_metadata = Nan::To<Object>(value);
    if (maybe_metadata.IsEmpty()) {
      return false;
    }
    if (!CreateMetadataArray(maybe_metadata.ToLocalChecked(), &send_metadata)) {
      return false;
    }
    out->data.send_initial_metadata.count = send_metadata.count;
    out->data.send_initial_metadata.metadata = send_metadata.metadata;
    return true;
  }
  bool IsFinalOp() { return false; }
  void OnComplete(bool success) {}

 protected:
  std::string GetTypeString() const { return "send_metadata"; }

 private:
  grpc_metadata_array send_metadata;
};

class SendMessageOp : public Op {
 public:
  SendMessageOp() { send_message = NULL; }
  ~SendMessageOp() {
    if (send_message != NULL) {
      grpc_byte_buffer_destroy(send_message);
    }
  }
  Local<Value> GetNodeValue() const {
    EscapableHandleScope scope;
    return scope.Escape(Nan::True());
  }
  bool ParseOp(Local<Value> value, grpc_op *out) {
    if (!::node::Buffer::HasInstance(value)) {
      return false;
    }
    Local<Object> object_value = Nan::To<Object>(value).ToLocalChecked();
    MaybeLocal<Value> maybe_flag_value =
        Nan::Get(object_value, Nan::New("grpcWriteFlags").ToLocalChecked());
    if (!maybe_flag_value.IsEmpty()) {
      Local<Value> flag_value = maybe_flag_value.ToLocalChecked();
      if (flag_value->IsUint32()) {
        Maybe<uint32_t> maybe_flag = Nan::To<uint32_t>(flag_value);
        out->flags = maybe_flag.FromMaybe(0) & GRPC_WRITE_USED_MASK;
      }
    }
    send_message = BufferToByteBuffer(value);
    out->data.send_message.send_message = send_message;
    return true;
  }

  bool IsFinalOp() { return false; }
  void OnComplete(bool success) {}

 protected:
  std::string GetTypeString() const { return "send_message"; }

 private:
  grpc_byte_buffer *send_message;
};

class SendClientCloseOp : public Op {
 public:
  Local<Value> GetNodeValue() const {
    EscapableHandleScope scope;
    return scope.Escape(Nan::True());
  }

  bool ParseOp(Local<Value> value, grpc_op *out) { return true; }
  bool IsFinalOp() { return false; }
  void OnComplete(bool success) {}

 protected:
  std::string GetTypeString() const { return "client_close"; }
};

class SendServerStatusOp : public Op {
 public:
  SendServerStatusOp() { grpc_metadata_array_init(&status_metadata); }
  ~SendServerStatusOp() {
    grpc_slice_unref(details);
    DestroyMetadataArray(&status_metadata);
  }
  Local<Value> GetNodeValue() const {
    EscapableHandleScope scope;
    return scope.Escape(Nan::True());
  }
  bool ParseOp(Local<Value> value, grpc_op *out) {
    if (!value->IsObject()) {
      return false;
    }
    Local<Object> server_status = Nan::To<Object>(value).ToLocalChecked();
    MaybeLocal<Value> maybe_metadata =
        Nan::Get(server_status, Nan::New("metadata").ToLocalChecked());
    if (maybe_metadata.IsEmpty()) {
      return false;
    }
    if (!maybe_metadata.ToLocalChecked()->IsObject()) {
      return false;
    }
    Local<Object> metadata =
        Nan::To<Object>(maybe_metadata.ToLocalChecked()).ToLocalChecked();
    MaybeLocal<Value> maybe_code =
        Nan::Get(server_status, Nan::New("code").ToLocalChecked());
    if (maybe_code.IsEmpty()) {
      return false;
    }
    if (!maybe_code.ToLocalChecked()->IsUint32()) {
      return false;
    }
    uint32_t code = Nan::To<uint32_t>(maybe_code.ToLocalChecked()).FromJust();
    MaybeLocal<Value> maybe_details =
        Nan::Get(server_status, Nan::New("details").ToLocalChecked());
    if (maybe_details.IsEmpty()) {
      return false;
    }
    if (!maybe_details.ToLocalChecked()->IsString()) {
      return false;
    }
    Local<String> details =
        Nan::To<String>(maybe_details.ToLocalChecked()).ToLocalChecked();
    if (!CreateMetadataArray(metadata, &status_metadata)) {
      return false;
    }
    out->data.send_status_from_server.trailing_metadata_count =
        status_metadata.count;
    out->data.send_status_from_server.trailing_metadata =
        status_metadata.metadata;
    out->data.send_status_from_server.status =
        static_cast<grpc_status_code>(code);
    this->details = CreateSliceFromString(details);
    out->data.send_status_from_server.status_details = &this->details;
    return true;
  }
  bool IsFinalOp() { return true; }
  void OnComplete(bool success) {}

 protected:
  std::string GetTypeString() const { return "send_status"; }

 private:
  grpc_slice details;
  grpc_metadata_array status_metadata;
};

class GetMetadataOp : public Op {
 public:
  GetMetadataOp() { grpc_metadata_array_init(&recv_metadata); }

  ~GetMetadataOp() { grpc_metadata_array_destroy(&recv_metadata); }

  Local<Value> GetNodeValue() const {
    EscapableHandleScope scope;
    return scope.Escape(ParseMetadata(&recv_metadata));
  }

  bool ParseOp(Local<Value> value, grpc_op *out) {
    out->data.recv_initial_metadata.recv_initial_metadata = &recv_metadata;
    return true;
  }
  bool IsFinalOp() { return false; }
  void OnComplete(bool success) {}

 protected:
  std::string GetTypeString() const { return "metadata"; }

 private:
  grpc_metadata_array recv_metadata;
};

class ReadMessageOp : public Op {
 public:
  ReadMessageOp() { recv_message = NULL; }
  ~ReadMessageOp() {
    if (recv_message != NULL) {
      grpc_byte_buffer_destroy(recv_message);
    }
  }
  Local<Value> GetNodeValue() const {
    EscapableHandleScope scope;
    return scope.Escape(ByteBufferToBuffer(recv_message));
  }

  bool ParseOp(Local<Value> value, grpc_op *out) {
    out->data.recv_message.recv_message = &recv_message;
    return true;
  }
  bool IsFinalOp() { return false; }
  void OnComplete(bool success) {}

 protected:
  std::string GetTypeString() const { return "read"; }

 private:
  grpc_byte_buffer *recv_message;
};

class ClientStatusOp : public Op {
 public:
  ClientStatusOp() { grpc_metadata_array_init(&metadata_array); }

  ~ClientStatusOp() { grpc_metadata_array_destroy(&metadata_array); }

  bool ParseOp(Local<Value> value, grpc_op *out) {
    out->data.recv_status_on_client.trailing_metadata = &metadata_array;
    out->data.recv_status_on_client.status = &status;
    out->data.recv_status_on_client.status_details = &status_details;
    return true;
  }

  Local<Value> GetNodeValue() const {
    EscapableHandleScope scope;
    Local<Object> status_obj = Nan::New<Object>();
    Nan::Set(status_obj, Nan::New("code").ToLocalChecked(),
             Nan::New<Number>(status));
    Nan::Set(status_obj, Nan::New("details").ToLocalChecked(),
             CopyStringFromSlice(status_details));
    Nan::Set(status_obj, Nan::New("metadata").ToLocalChecked(),
             ParseMetadata(&metadata_array));
    return scope.Escape(status_obj);
  }
  bool IsFinalOp() { return true; }
  void OnComplete(bool success) {}

 protected:
  std::string GetTypeString() const { return "status"; }

 private:
  grpc_metadata_array metadata_array;
  grpc_status_code status;
  grpc_slice status_details;
};

class ServerCloseResponseOp : public Op {
 public:
  Local<Value> GetNodeValue() const {
    EscapableHandleScope scope;
    return scope.Escape(Nan::New<Boolean>(cancelled));
  }

  bool ParseOp(Local<Value> value, grpc_op *out) {
    out->data.recv_close_on_server.cancelled = &cancelled;
    return true;
  }
  bool IsFinalOp() { return false; }
  void OnComplete(bool success) {}

 protected:
  std::string GetTypeString() const { return "cancelled"; }

 private:
  int cancelled;
};

tag::tag(Callback *callback, OpVec *ops, Call *call, Local<Value> call_value)
    : callback(callback), ops(ops), call(call) {
  HandleScope scope;
  call_persist.Reset(call_value);
}

tag::~tag() {
  delete callback;
  delete ops;
}

void CompleteTag(void *tag, const char *error_message) {
  HandleScope scope;
  struct tag *tag_struct = reinterpret_cast<struct tag *>(tag);
  Callback *callback = tag_struct->callback;
  if (error_message == NULL) {
    Local<Object> tag_obj = Nan::New<Object>();
    for (vector<unique_ptr<Op> >::iterator it = tag_struct->ops->begin();
         it != tag_struct->ops->end(); ++it) {
      Op *op_ptr = it->get();
      Nan::Set(tag_obj, op_ptr->GetOpType(), op_ptr->GetNodeValue());
    }
    Local<Value> argv[] = {Nan::Null(), tag_obj};
    callback->Call(2, argv);
  } else {
    Local<Value> argv[] = {Nan::Error(error_message)};
    callback->Call(1, argv);
  }
  bool success = (error_message == NULL);
  bool is_final_op = false;
  for (vector<unique_ptr<Op> >::iterator it = tag_struct->ops->begin();
       it != tag_struct->ops->end(); ++it) {
    Op *op_ptr = it->get();
    op_ptr->OnComplete(success);
    if (op_ptr->IsFinalOp()) {
      is_final_op = true;
    }
  }
  if (tag_struct->call == NULL) {
    return;
  }
  tag_struct->call->CompleteBatch(is_final_op);
}

void DestroyTag(void *tag) {
  struct tag *tag_struct = reinterpret_cast<struct tag *>(tag);
  delete tag_struct;
}

void Call::DestroyCall() {
  if (this->wrapped_call != NULL) {
    grpc_call_unref(this->wrapped_call);
    this->wrapped_call = NULL;
  }
}

Call::Call(grpc_call *call)
    : wrapped_call(call), pending_batches(0), has_final_op_completed(false) {
  peer = grpc_call_get_peer(call);
}

Call::~Call() {
  DestroyCall();
  gpr_free(peer);
}

void Call::Init(Local<Object> exports) {
  HandleScope scope;
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("Call").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  Nan::SetPrototypeMethod(tpl, "startBatch", StartBatch);
  Nan::SetPrototypeMethod(tpl, "cancel", Cancel);
  Nan::SetPrototypeMethod(tpl, "cancelWithStatus", CancelWithStatus);
  Nan::SetPrototypeMethod(tpl, "getPeer", GetPeer);
  Nan::SetPrototypeMethod(tpl, "setCredentials", SetCredentials);
  fun_tpl.Reset(tpl);
  Local<Function> ctr = Nan::GetFunction(tpl).ToLocalChecked();
  Nan::Set(exports, Nan::New("Call").ToLocalChecked(), ctr);
  constructor = new Callback(ctr);
}

bool Call::HasInstance(Local<Value> val) {
  HandleScope scope;
  return Nan::New(fun_tpl)->HasInstance(val);
}

Local<Value> Call::WrapStruct(grpc_call *call) {
  EscapableHandleScope scope;
  if (call == NULL) {
    return scope.Escape(Nan::Null());
  }
  const int argc = 1;
  Local<Value> argv[argc] = {
      Nan::New<External>(reinterpret_cast<void *>(call))};
  MaybeLocal<Object> maybe_instance =
      Nan::NewInstance(constructor->GetFunction(), argc, argv);
  if (maybe_instance.IsEmpty()) {
    return scope.Escape(Nan::Null());
  } else {
    return scope.Escape(maybe_instance.ToLocalChecked());
  }
}

void Call::CompleteBatch(bool is_final_op) {
  if (is_final_op) {
    this->has_final_op_completed = true;
  }
  this->pending_batches--;
  if (this->has_final_op_completed && this->pending_batches == 0) {
    this->DestroyCall();
  }
}

NAN_METHOD(Call::New) {
  /* Arguments:
   * 0: Channel to make the call on
   * 1: Method
   * 2: Deadline
   * 3: host
   * 4: parent Call
   * 5: propagation flags
   */
  if (info.IsConstructCall()) {
    Call *call;
    if (info[0]->IsExternal()) {
      Local<External> ext = info[0].As<External>();
      // This option is used for wrapping an existing call
      grpc_call *call_value = reinterpret_cast<grpc_call *>(ext->Value());
      call = new Call(call_value);
    } else {
      if (!Channel::HasInstance(info[0])) {
        return Nan::ThrowTypeError("Call's first argument must be a Channel");
      }
      if (!info[1]->IsString()) {
        return Nan::ThrowTypeError("Call's second argument must be a string");
      }
      if (!(info[2]->IsNumber() || info[2]->IsDate())) {
        return Nan::ThrowTypeError(
            "Call's third argument must be a date or a number");
      }
      // These arguments are at the end because they are optional
      grpc_call *parent_call = NULL;
      if (Call::HasInstance(info[4])) {
        Call *parent_obj =
            ObjectWrap::Unwrap<Call>(Nan::To<Object>(info[4]).ToLocalChecked());
        parent_call = parent_obj->wrapped_call;
      } else if (!(info[4]->IsUndefined() || info[4]->IsNull())) {
        return Nan::ThrowTypeError(
            "Call's fifth argument must be another call, if provided");
      }
      uint32_t propagate_flags = GRPC_PROPAGATE_DEFAULTS;
      if (info[5]->IsUint32()) {
        propagate_flags = Nan::To<uint32_t>(info[5]).FromJust();
      } else if (!(info[5]->IsUndefined() || info[5]->IsNull())) {
        return Nan::ThrowTypeError(
            "Call's sixth argument must be propagate flags, if provided");
      }
      Local<Object> channel_object = Nan::To<Object>(info[0]).ToLocalChecked();
      Channel *channel = ObjectWrap::Unwrap<Channel>(channel_object);
      if (channel->GetWrappedChannel() == NULL) {
        return Nan::ThrowError("Call cannot be created from a closed channel");
      }
      double deadline = Nan::To<double>(info[2]).FromJust();
      grpc_channel *wrapped_channel = channel->GetWrappedChannel();
      grpc_call *wrapped_call;
      grpc_slice method =
          CreateSliceFromString(Nan::To<String>(info[1]).ToLocalChecked());
      if (info[3]->IsString()) {
        grpc_slice *host = new grpc_slice;
        *host =
            CreateSliceFromString(Nan::To<String>(info[3]).ToLocalChecked());
        wrapped_call = grpc_channel_create_call(
            wrapped_channel, parent_call, propagate_flags, GetCompletionQueue(),
            method, host, MillisecondsToTimespec(deadline), NULL);
        delete host;
      } else if (info[3]->IsUndefined() || info[3]->IsNull()) {
        wrapped_call = grpc_channel_create_call(
            wrapped_channel, parent_call, propagate_flags, GetCompletionQueue(),
            method, NULL, MillisecondsToTimespec(deadline), NULL);
      } else {
        return Nan::ThrowTypeError("Call's fourth argument must be a string");
      }
      grpc_slice_unref(method);
      call = new Call(wrapped_call);
      Nan::Set(info.This(), Nan::New("channel_").ToLocalChecked(),
               channel_object);
    }
    call->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    const int argc = 4;
    Local<Value> argv[argc] = {info[0], info[1], info[2], info[3]};
    MaybeLocal<Object> maybe_instance =
        Nan::NewInstance(constructor->GetFunction(), argc, argv);
    if (maybe_instance.IsEmpty()) {
      // There's probably a pending exception
      return;
    } else {
      info.GetReturnValue().Set(maybe_instance.ToLocalChecked());
    }
  }
}

NAN_METHOD(Call::StartBatch) {
  if (!Call::HasInstance(info.This())) {
    return Nan::ThrowTypeError("startBatch can only be called on Call objects");
  }
  if (!info[0]->IsObject()) {
    return Nan::ThrowError("startBatch's first argument must be an object");
  }
  if (!info[1]->IsFunction()) {
    return Nan::ThrowError("startBatch's second argument must be a callback");
  }
  Local<Function> callback_func = info[1].As<Function>();
  Call *call = ObjectWrap::Unwrap<Call>(info.This());
  if (call->wrapped_call == NULL) {
    /* This implies that the call has completed and has been destroyed. To
     * emulate
     * previous behavior, we should call the callback immediately with an error,
     * as though the batch had failed in core */
    Local<Value> argv[] = {
        Nan::Error("The async function failed because the call has completed")};
    Nan::Call(callback_func, Nan::New<Object>(), 1, argv);
    return;
  }
  Local<Object> obj = Nan::To<Object>(info[0]).ToLocalChecked();
  Local<Array> keys = Nan::GetOwnPropertyNames(obj).ToLocalChecked();
  size_t nops = keys->Length();
  vector<grpc_op> ops(nops);
  unique_ptr<OpVec> op_vector(new OpVec());
  for (unsigned int i = 0; i < nops; i++) {
    unique_ptr<Op> op;
    MaybeLocal<Value> maybe_key = Nan::Get(keys, i);
    if (maybe_key.IsEmpty() || (!maybe_key.ToLocalChecked()->IsUint32())) {
      return Nan::ThrowError(
          "startBatch's first argument's keys must be integers");
    }
    uint32_t type = Nan::To<uint32_t>(maybe_key.ToLocalChecked()).FromJust();
    ops[i].op = static_cast<grpc_op_type>(type);
    ops[i].flags = 0;
    ops[i].reserved = NULL;
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
        return Nan::ThrowError("Argument object had an unrecognized key");
    }
    if (!op->ParseOp(obj->Get(type), &ops[i])) {
      return Nan::ThrowTypeError("Incorrectly typed arguments to startBatch");
    }
    op_vector->push_back(std::move(op));
  }
  Callback *callback = new Callback(callback_func);
  grpc_call_error error = grpc_call_start_batch(
      call->wrapped_call, &ops[0], nops,
      new struct tag(callback, op_vector.release(), call, info.This()), NULL);
  if (error != GRPC_CALL_OK) {
    return Nan::ThrowError(nanErrorWithCode("startBatch failed", error));
  }
  call->pending_batches++;
  CompletionQueueNext();
}

NAN_METHOD(Call::Cancel) {
  if (!Call::HasInstance(info.This())) {
    return Nan::ThrowTypeError("cancel can only be called on Call objects");
  }
  Call *call = ObjectWrap::Unwrap<Call>(info.This());
  if (call->wrapped_call == NULL) {
    /* Cancel is supposed to be idempotent. If the call has already finished,
     * cancel should just complete silently */
    return;
  }
  grpc_call_error error = grpc_call_cancel(call->wrapped_call, NULL);
  if (error != GRPC_CALL_OK) {
    return Nan::ThrowError(nanErrorWithCode("cancel failed", error));
  }
}

NAN_METHOD(Call::CancelWithStatus) {
  Nan::HandleScope scope;
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError("cancel can only be called on Call objects");
  }
  if (!info[0]->IsUint32()) {
    return Nan::ThrowTypeError(
        "cancelWithStatus's first argument must be a status code");
  }
  if (!info[1]->IsString()) {
    return Nan::ThrowTypeError(
        "cancelWithStatus's second argument must be a string");
  }
  Call *call = ObjectWrap::Unwrap<Call>(info.This());
  if (call->wrapped_call == NULL) {
    /* Cancel is supposed to be idempotent. If the call has already finished,
     * cancel should just complete silently */
    return;
  }
  grpc_status_code code =
      static_cast<grpc_status_code>(Nan::To<uint32_t>(info[0]).FromJust());
  if (code == GRPC_STATUS_OK) {
    return Nan::ThrowRangeError(
        "cancelWithStatus cannot be called with OK status");
  }
  Utf8String details(info[1]);
  grpc_call_cancel_with_status(call->wrapped_call, code, *details, NULL);
}

NAN_METHOD(Call::GetPeer) {
  Nan::HandleScope scope;
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError("getPeer can only be called on Call objects");
  }
  Call *call = ObjectWrap::Unwrap<Call>(info.This());
  Local<Value> peer_value = Nan::New(call->peer).ToLocalChecked();
  info.GetReturnValue().Set(peer_value);
}

NAN_METHOD(Call::SetCredentials) {
  Nan::HandleScope scope;
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError(
        "setCredentials can only be called on Call objects");
  }
  if (!CallCredentials::HasInstance(info[0])) {
    return Nan::ThrowTypeError(
        "setCredentials' first argument must be a CallCredentials");
  }
  Call *call = ObjectWrap::Unwrap<Call>(info.This());
  if (call->wrapped_call == NULL) {
    return Nan::ThrowError(
        "Cannot set credentials on a call that has already started");
  }
  CallCredentials *creds_object = ObjectWrap::Unwrap<CallCredentials>(
      Nan::To<Object>(info[0]).ToLocalChecked());
  grpc_call_credentials *creds = creds_object->GetWrappedCredentials();
  grpc_call_error error = GRPC_CALL_ERROR;
  if (creds) {
    error = grpc_call_set_credentials(call->wrapped_call, creds);
  }
  info.GetReturnValue().Set(Nan::New<Uint32>(error));
}

}  // namespace node
}  // namespace grpc
