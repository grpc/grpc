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

#include <vector>

#include <node.h>

#include "grpc/support/log.h"
#include "grpc/grpc.h"
#include "grpc/support/time.h"
#include "byte_buffer.h"
#include "call.h"
#include "channel.h"
#include "completion_queue_async_worker.h"
#include "timeval.h"
#include "tag.h"

namespace grpc {
namespace node {

using ::node::Buffer;
using v8::Arguments;
using v8::Array;
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

Persistent<Function> Call::constructor;
Persistent<FunctionTemplate> Call::fun_tpl;

bool CreateMetadataArray(Handle<Object> metadata, grpc_metadata_array *array
                         vector<NanUtf8String*> *string_handles,
                         vector<Persistent<Value>*> *handles) {
  NanScope();
  Handle<Array> keys(metadata->GetOwnPropertyNames());
  for (unsigned int i = 0; i < keys->Length(); i++) {
    Handle<String> current_key(keys->Get(i)->ToString());
    if (!metadata->Get(current_key)->IsArray()) {
      return false;
    }
    array->capacity += Local<Array>::Cast(metadata->Get(current_key))->Length();
  }
  array->metadata = calloc(array->capacity, sizeof(grpc_metadata));
  for (unsigned int i = 0; i < keys->Length(); i++) {
    Handle<String> current_key(keys->Get(i)->ToString());
    NanUtf8String *utf8_key = new NanUtf8String(current_key);
    string_handles->push_back(utf8_key);
    Handle<Array> values = Local<Array>::Cast(metadata->Get(current_key));
    for (unsigned int j = 0; j < values->Length(); j++) {
      Handle<Value> value = values->Get(j);
      grpc_metadata *current = &array[array->count];
      grpc_call_error error;
      current->key = **utf8_key;
      if (Buffer::HasInstance(value)) {
        current->value = Buffer::Data(value);
        current->value_length = Buffer::Length(value);
        Persistent<Value> *handle = new Persistent<Value>();
        NanAssignPersistent(handle, object);
        handles->push_back(handle);
      } else if (value->IsString()) {
        Handle<String> string_value = value->ToString();
        NanUtf8String *utf8_value = new NanUtf8String(string_value);
        string_handles->push_back(utf8_value);
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

Call::Call(grpc_call *call) : wrapped_call(call) {}

Call::~Call() { grpc_call_destroy(wrapped_call); }

void Call::Init(Handle<Object> exports) {
  NanScope();
  Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
  tpl->SetClassName(NanNew("Call"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  NanSetPrototypeTemplate(tpl, "addMetadata",
                          FunctionTemplate::New(AddMetadata)->GetFunction());
  NanSetPrototypeTemplate(tpl, "invoke",
                          FunctionTemplate::New(Invoke)->GetFunction());
  NanSetPrototypeTemplate(tpl, "serverAccept",
                          FunctionTemplate::New(ServerAccept)->GetFunction());
  NanSetPrototypeTemplate(
      tpl, "serverEndInitialMetadata",
      FunctionTemplate::New(ServerEndInitialMetadata)->GetFunction());
  NanSetPrototypeTemplate(tpl, "cancel",
                          FunctionTemplate::New(Cancel)->GetFunction());
  NanSetPrototypeTemplate(tpl, "startWrite",
                          FunctionTemplate::New(StartWrite)->GetFunction());
  NanSetPrototypeTemplate(
      tpl, "startWriteStatus",
      FunctionTemplate::New(StartWriteStatus)->GetFunction());
  NanSetPrototypeTemplate(tpl, "writesDone",
                          FunctionTemplate::New(WritesDone)->GetFunction());
  NanSetPrototypeTemplate(tpl, "startReadMetadata",
                          FunctionTemplate::New(WritesDone)->GetFunction());
  NanSetPrototypeTemplate(tpl, "startRead",
                          FunctionTemplate::New(StartRead)->GetFunction());
  NanAssignPersistent(fun_tpl, tpl);
  NanAssignPersistent(constructor, tpl->GetFunction());
  constructor->Set(NanNew("WRITE_BUFFER_HINT"),
                   NanNew<Uint32, uint32_t>(GRPC_WRITE_BUFFER_HINT));
  constructor->Set(NanNew("WRITE_NO_COMPRESS"),
                   NanNew<Uint32, uint32_t>(GRPC_WRITE_NO_COMPRESS));
  exports->Set(String::NewSymbol("Call"), constructor);
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
  Handle<Value> argv[argc] = {External::New(reinterpret_cast<void *>(call))};
  return NanEscapeScope(constructor->NewInstance(argc, argv));
}

NAN_METHOD(Call::New) {
  NanScope();

  if (args.IsConstructCall()) {
    Call *call;
    if (args[0]->IsExternal()) {
      // This option is used for wrapping an existing call
      grpc_call *call_value =
          reinterpret_cast<grpc_call *>(External::Unwrap(args[0]));
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
      args.This()->SetHiddenValue(String::NewSymbol("channel_"),
                                  channel_object);
    }
    call->Wrap(args.This());
    NanReturnValue(args.This());
  } else {
    const int argc = 4;
    Local<Value> argv[argc] = {args[0], args[1], args[2], args[3]};
    NanReturnValue(constructor->NewInstance(argc, argv));
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
  vector<Persistent<Value> *> *handles = new vector<Persistent<Value>>();
  vector<NanUtf8String *> *strings = new vector<NanUtf8String *>();
  Persistent<Value> *handle;
  Handle<Array> keys = args[0]->GetOwnPropertyNames();
  size_t nops = keys->Length();
  grpc_op *ops = calloc(nops, sizeof(grpc_op));
  grpc_metadata_array array;
  Handle<Object> server_status;
  NanUtf8String *str;
  for (unsigned int i = 0; i < nops; i++) {
    if (!keys->Get(i)->IsUInt32()) {
      return NanThrowError(
          "startBatch's first argument's keys must be integers");
    }
    uint32_t type = keys->Get(i)->UInt32Value();
    ops[i].op = type;
    switch (type) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        if (!args[0]->Get(type)->IsObject()) {
          return NanThrowError("metadata must be an object");
        }
        if (!CreateMetadataArray(args[0]->Get(type)->ToObject(), &array,
                                 strings, handles)) {
          return NanThrowError("failed to parse metadata");
        }
        ops[i].data.send_initial_metadata.count = array.count;
        ops[i].data.send_initial_metadata.metadata = array.metadata;
        break
      case GRPC_OP_SEND_MESSAGE:
        if (!Buffer::HasInstance(args[0]->Get(type))) {
          return NanThrowError("message must be a Buffer");
        }
        ops[i].data.send_message = BufferToByteBuffer(args[0]->Get(type));
        handle = new Persistent<Value>();
        NanAssignPersistent(*handle, args[0]->Get(type));
        handles->push_back(handle);
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        if (!args[0]->Get(type)->IsObject()) {
          return NanThrowError("server status must be an object");
        }
        server_status = args[0]->Get(type)->ToObject();
        if (!server_status->Get("metadata")->IsObject()) {
          return NanThrowError("status metadata must be an object");
        }
        if (!server_status->Get("code")->IsUInt32()) {
          return NanThrowError("status code must be a positive integer");
        }
        if (!server_status->Get("details")->IsString()) {
          return NanThrowError("status details must be a string");
        }
        if (!CreateMetadataArray(server_status->Get("metadata")->ToObject(),
                                 &array, strings, handles)) {
          return NanThrowError("Failed to parse status metadata");
        }
        ops[i].data.send_status_from_server.trailing_metadata_count =
            array.count;
        ops[i].data.send_status_from_server.trailing_metadata = array.metadata;
        ops[i].data.send_status_from_server.status =
            server_status->Get("code")->UInt32Value();
        str = new NanUtf8String(server_status->Get("details"));
        strings->push_back(str);
        ops[i].data.send_status_from_server.status_details = **str;
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        ops[i].data.recv_initial_metadata = malloc(sizeof(grpc_metadata_array));
        grpc_metadata_array_init(ops[i].data.recv_initial_metadata);
        break;
      case GRPC_OP_RECV_MESSAGE:
        ops[i].data.recv_message = malloc(sizeof(grpc_byte_buffer*));
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        ops[i].data.recv_status_on_client.trailing_metadata =
            malloc(sizeof(grpc_metadata_array));
        grpc_metadata_array_init(ops[i].data.recv_status_on_client);
        ops[i].data.recv_status_on_client.status =
            malloc(sizeof(grpc_status_code));
        ops[i].data.recv_status_on_client.status_details =
            malloc(sizeof(char *));
        ops[i].data.recv_status_on_client.status_details_capacity =
            malloc(sizeof(size_t));
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        ops[i].data.recv_close_on_server = malloc(sizeof(int));
        break;

    }
  }
}

NAN_METHOD(Call::AddMetadata) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("addMetadata can only be called on Call objects");
  }
  Call *call = ObjectWrap::Unwrap<Call>(args.This());
  if (!args[0]->IsObject()) {
    return NanThrowTypeError("addMetadata's first argument must be an object");
  }
  Handle<Object> metadata = args[0]->ToObject();
  Handle<Array> keys(metadata->GetOwnPropertyNames());
  for (unsigned int i = 0; i < keys->Length(); i++) {
    Handle<String> current_key(keys->Get(i)->ToString());
    if (!metadata->Get(current_key)->IsArray()) {
      return NanThrowTypeError(
          "addMetadata's first argument's values must be arrays");
    }
    NanUtf8String utf8_key(current_key);
    Handle<Array> values = Local<Array>::Cast(metadata->Get(current_key));
    for (unsigned int j = 0; j < values->Length(); j++) {
      Handle<Value> value = values->Get(j);
      grpc_metadata metadata;
      grpc_call_error error;
      metadata.key = *utf8_key;
      if (Buffer::HasInstance(value)) {
        metadata.value = Buffer::Data(value);
        metadata.value_length = Buffer::Length(value);
        error = grpc_call_add_metadata_old(call->wrapped_call, &metadata, 0);
      } else if (value->IsString()) {
        Handle<String> string_value = value->ToString();
        NanUtf8String utf8_value(string_value);
        metadata.value = *utf8_value;
        metadata.value_length = string_value->Length();
        gpr_log(GPR_DEBUG, "adding metadata: %s, %s, %d", metadata.key,
                metadata.value, metadata.value_length);
        error = grpc_call_add_metadata_old(call->wrapped_call, &metadata, 0);
      } else {
        return NanThrowTypeError(
            "addMetadata values must be strings or buffers");
      }
      if (error != GRPC_CALL_OK) {
        return NanThrowError("addMetadata failed", error);
      }
    }
  }
  NanReturnUndefined();
}

NAN_METHOD(Call::Invoke) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("invoke can only be called on Call objects");
  }
  if (!args[0]->IsFunction()) {
    return NanThrowTypeError("invoke's first argument must be a function");
  }
  if (!args[1]->IsFunction()) {
    return NanThrowTypeError("invoke's second argument must be a function");
  }
  if (!args[2]->IsUint32()) {
    return NanThrowTypeError("invoke's third argument must be integer flags");
  }
  Call *call = ObjectWrap::Unwrap<Call>(args.This());
  unsigned int flags = args[3]->Uint32Value();
  grpc_call_error error = grpc_call_invoke_old(
      call->wrapped_call, CompletionQueueAsyncWorker::GetQueue(),
      CreateTag(args[0], args.This()), CreateTag(args[1], args.This()), flags);
  if (error == GRPC_CALL_OK) {
    CompletionQueueAsyncWorker::Next();
    CompletionQueueAsyncWorker::Next();
  } else {
    return NanThrowError("invoke failed", error);
  }
  NanReturnUndefined();
}

NAN_METHOD(Call::ServerAccept) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("accept can only be called on Call objects");
  }
  if (!args[0]->IsFunction()) {
    return NanThrowTypeError("accept's first argument must be a function");
  }
  Call *call = ObjectWrap::Unwrap<Call>(args.This());
  grpc_call_error error = grpc_call_server_accept_old(
      call->wrapped_call, CompletionQueueAsyncWorker::GetQueue(),
      CreateTag(args[0], args.This()));
  if (error == GRPC_CALL_OK) {
    CompletionQueueAsyncWorker::Next();
  } else {
    return NanThrowError("serverAccept failed", error);
  }
  NanReturnUndefined();
}

NAN_METHOD(Call::ServerEndInitialMetadata) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError(
        "serverEndInitialMetadata can only be called on Call objects");
  }
  if (!args[0]->IsUint32()) {
    return NanThrowTypeError(
        "serverEndInitialMetadata's second argument must be integer flags");
  }
  Call *call = ObjectWrap::Unwrap<Call>(args.This());
  unsigned int flags = args[1]->Uint32Value();
  grpc_call_error error =
      grpc_call_server_end_initial_metadata_old(call->wrapped_call, flags);
  if (error != GRPC_CALL_OK) {
    return NanThrowError("serverEndInitialMetadata failed", error);
  }
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

NAN_METHOD(Call::StartWrite) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("startWrite can only be called on Call objects");
  }
  if (!Buffer::HasInstance(args[0])) {
    return NanThrowTypeError("startWrite's first argument must be a Buffer");
  }
  if (!args[1]->IsFunction()) {
    return NanThrowTypeError("startWrite's second argument must be a function");
  }
  if (!args[2]->IsUint32()) {
    return NanThrowTypeError(
        "startWrite's third argument must be integer flags");
  }
  Call *call = ObjectWrap::Unwrap<Call>(args.This());
  grpc_byte_buffer *buffer = BufferToByteBuffer(args[0]);
  unsigned int flags = args[2]->Uint32Value();
  grpc_call_error error = grpc_call_start_write_old(
      call->wrapped_call, buffer, CreateTag(args[1], args.This()), flags);
  if (error == GRPC_CALL_OK) {
    CompletionQueueAsyncWorker::Next();
  } else {
    return NanThrowError("startWrite failed", error);
  }
  NanReturnUndefined();
}

NAN_METHOD(Call::StartWriteStatus) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError(
        "startWriteStatus can only be called on Call objects");
  }
  if (!args[0]->IsUint32()) {
    return NanThrowTypeError(
        "startWriteStatus's first argument must be a status code");
  }
  if (!args[1]->IsString()) {
    return NanThrowTypeError(
        "startWriteStatus's second argument must be a string");
  }
  if (!args[2]->IsFunction()) {
    return NanThrowTypeError(
        "startWriteStatus's third argument must be a function");
  }
  Call *call = ObjectWrap::Unwrap<Call>(args.This());
  NanUtf8String details(args[1]);
  grpc_call_error error = grpc_call_start_write_status_old(
      call->wrapped_call, (grpc_status_code)args[0]->Uint32Value(), *details,
      CreateTag(args[2], args.This()));
  if (error == GRPC_CALL_OK) {
    CompletionQueueAsyncWorker::Next();
  } else {
    return NanThrowError("startWriteStatus failed", error);
  }
  NanReturnUndefined();
}

NAN_METHOD(Call::WritesDone) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("writesDone can only be called on Call objects");
  }
  if (!args[0]->IsFunction()) {
    return NanThrowTypeError("writesDone's first argument must be a function");
  }
  Call *call = ObjectWrap::Unwrap<Call>(args.This());
  grpc_call_error error = grpc_call_writes_done_old(
      call->wrapped_call, CreateTag(args[0], args.This()));
  if (error == GRPC_CALL_OK) {
    CompletionQueueAsyncWorker::Next();
  } else {
    return NanThrowError("writesDone failed", error);
  }
  NanReturnUndefined();
}

NAN_METHOD(Call::StartRead) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("startRead can only be called on Call objects");
  }
  if (!args[0]->IsFunction()) {
    return NanThrowTypeError("startRead's first argument must be a function");
  }
  Call *call = ObjectWrap::Unwrap<Call>(args.This());
  grpc_call_error error = grpc_call_start_read_old(
      call->wrapped_call, CreateTag(args[0], args.This()));
  if (error == GRPC_CALL_OK) {
    CompletionQueueAsyncWorker::Next();
  } else {
    return NanThrowError("startRead failed", error);
  }
  NanReturnUndefined();
}

}  // namespace node
}  // namespace grpc
