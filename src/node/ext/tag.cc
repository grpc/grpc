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

#include <map>
#include <vector>

#include <grpc/grpc.h>
#include <stdlib.h>
#include <node.h>
#include <nan.h>
#include "tag.h"
#include "call.h"

namespace grpc {
namespace node {

using v8::Boolean;
using v8::Function;
using v8::Handle;
using v8::HandleScope;
using v8::Persistent;
using v8::Value;

Handle<Value> ParseMetadata(grpc_metadata_array *metadata_array) {
  NanEscapableScope();
  grpc_metadata *metadata_elements = metadata_array->metadata;
  size_t length = metadata_array->count;
  std::map<char*, size_t> size_map;
  std::map<char*, size_t> index_map;

  for (unsigned int i = 0; i < length; i++) {
    char *key = metadata_elements[i].key;
    if (size_map.count(key)) {
      size_map[key] += 1;
    }
    index_map[key] = 0;
  }
  Handle<Object> metadata_object = NanNew<Object>();
  for (unsigned int i = 0; i < length; i++) {
    grpc_metadata* elem = &metadata_elements[i];
    Handle<String> key_string = String::New(elem->key);
    Handle<Array> array;
    if (metadata_object->Has(key_string)) {
      array = Handle<Array>::Cast(metadata_object->Get(key_string));
    } else {
      array = NanNew<Array>(size_map[elem->key]);
      metadata_object->Set(key_string, array);
    }
    array->Set(index_map[elem->key],
               MakeFastBuffer(
                   NanNewBufferHandle(elem->value, elem->value_length)));
    index_map[elem->key] += 1;
  }
  return NanEscapeScope(metadata_object);
}

class OpResponse {
 public:
  explicit OpResponse(char *name): name(name) {
  }
  virtual Handle<Value> GetNodeValue() const = 0;
  Handle<Value> GetOpType() const {
    NanEscapableScope();
    return NanEscapeScope(NanNew(name));
  }

 private:
  char *name;
};

class SendResponse : public OpResponse {
 public:
  explicit SendResponse(char *name): OpResponse(name) {
  }

  Handle<Value> GetNodeValue() {
    NanEscapableScope();
    return NanEscapeScope(NanTrue());
  }
}

class MetadataResponse : public OpResponse {
 public:
  explicit MetadataResponse(grpc_metadata_array *recv_metadata):
      recv_metadata(recv_metadata), OpResponse("metadata") {
  }

  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    return NanEscapeScope(ParseMetadata(recv_metadata));
  }

 private:
  grpc_metadata_array *recv_metadata;
};

class MessageResponse : public OpResponse {
 public:
  explicit MessageResponse(grpc_byte_buffer **recv_message):
      recv_message(recv_message), OpResponse("read") {
  }

  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    return NanEscapeScope(ByteBufferToBuffer(*recv_message));
  }

 private:
  grpc_byte_buffer **recv_message
};

class ClientStatusResponse : public OpResponse {
 public:
  explicit ClientStatusResponse(grpc_metadata_array *metadata_array,
                                grpc_status_code *status,
                                char **status_details):
      metadata_array(metadata_array), status(status),
      status_details(status_details), OpResponse("status") {
  }

  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    Handle<Object> status_obj = NanNew<Object>();
    status_obj->Set(NanNew("code"), NanNew<Number>(*status));
    if (event->data.finished.details != NULL) {
      status_obj->Set(NanNew("details"), String::New(*status_details));
    }
    status_obj->Set(NanNew("metadata"), ParseMetadata(metadata_array));
    return NanEscapeScope(status_obj);
  }
 private:
  grpc_metadata_array *metadata_array;
  grpc_status_code *status;
  char **status_details;
};

class ServerCloseResponse : public OpResponse {
 public:
  explicit ServerCloseResponse(int *cancelled): cancelled(cancelled),
                                                OpResponse("cancelled") {
  }

  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    NanEscapeScope(NanNew<Boolean>(*cancelled));
  }

 private:
  int *cancelled;
};

class NewCallResponse : public OpResponse {
 public:
  explicit NewCallResponse(grpc_call **call, grpc_call_details *details,
                           grpc_metadata_array *request_metadata) :
      call(call), details(details), request_metadata(request_metadata),
      OpResponse("call"){
  }

  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    if (*call == NULL) {
      return NanEscapeScope(NanNull());
    }
    Handle<Object> obj = NanNew<Object>();
    obj->Set(NanNew("call"), Call::WrapStruct(*call));
    obj->Set(NanNew("method"), NanNew(details->method));
    obj->Set(NanNew("host"), NanNew(details->host));
    obj->Set(NanNew("deadline"),
             NanNew<Date>(TimespecToMilliseconds(details->deadline)));
    obj->Set(NanNew("metadata"), ParseMetadata(request_metadata));
    return NanEscapeScope(obj);
  }
 private:
  grpc_call **call;
  grpc_call_details *details;
  grpc_metadata_array *request_metadata;
}

struct tag {
  tag(NanCallback *callback, std::vector<OpResponse*> *responses) :
      callback(callback), repsonses(responses) {
  }
  ~tag() {
    for (std::vector<OpResponse *>::iterator it = responses->begin();
       it != responses->end(); ++it) {
      delete *it;
    }
    delete callback;
    delete responses;
  }
  NanCallback *callback;
  std::vector<OpResponse*> *responses;
};

void *CreateTag(Handle<Function> callback, grpc_op *ops, size_t nops) {
  NanScope();
  NanCallback *cb = new NanCallback(callback);
  vector<OpResponse*> *responses = new vector<OpResponse*>();
  for (size_t i = 0; i < nops; i++) {
    grpc_op *op = &ops[i];
    OpResponse *resp;
    // Switching on the TYPE of the op
    switch (op->op) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        resp = new SendResponse("send metadata");
        break;
      case GRPC_OP_SEND_MESSAGE:
        resp = new SendResponse("write");
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        resp = new SendResponse("client close");
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        resp = new SendResponse("server close");
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
        resp = new MetadataResponse(op->data.recv_initial_metadata);
        break;
      case GRPC_OP_RECV_MESSAGE:
        resp = new MessageResponse(op->data.recv_message);
        break;
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        resp = new ClientStatusResponse(
            op->data.recv_status_on_client.trailing_metadata,
            op->data.recv_status_on_client.status,
            op->data.recv_status_on_client.status_details);
        break;
      case GRPC_RECV_CLOSE_ON_SERVER:
        resp = new ServerCloseResponse(op->data.recv_close_on_server.cancelled);
        break;
      default:
        continue;
    }
    responses->push_back(resp);
  }
  struct tag *tag_struct = new struct tag(cb, responses);
  return reinterpret_cast<void *>(tag_struct);
}

void *CreateTag(Handle<Function> callback, grpc_call **call,
                grpc_call_details *details,
                grpc_metadata_array *request_metadata) {
  NanEscapableScope();
  NanCallback *cb = new NanCallback(callback);
  vector<OpResponse*> *responses = new vector<OpResponse*>();
  OpResponse *resp = new NewCallResponse(call, details, request_metadata);
  responses->push_back(resp);
  struct tag *tag_struct = new struct tag(cb, responses);
  return reinterpret_cast<void *>(tag_struct);
}

NanCallback GetCallback(void *tag) {
  NanEscapableScope();
  struct tag *tag_struct = reinterpret_cast<struct tag *>(tag);
  return NanEscapeScope(*tag_struct->callback);
}

Handle<Value> GetNodeValue(void *tag) {
  NanEscapableScope();
  struct tag *tag_struct = reinterpret_cast<struct tag *>(tag);
  Handle<Object> obj = NanNew<Object>();
  for (std::vector<OpResponse *>::iterator it = tag_struct->responses->begin();
       it != tag_struct->responses->end(); ++it) {
    OpResponse *resp = *it;
    obj->Set(resp->GetOpType(), resp->GetNodeValue());
  }
  return NanEscapeScope(obj);
}

void DestroyTag(void *tag) { delete reinterpret_cast<struct tag *>(tag); }

}  // namespace node
}  // namespace grpc
