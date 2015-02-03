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

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"
#include "byte_buffer.h"
#include "call.h"
#include "event.h"
#include "tag.h"
#include "timeval.h"

namespace grpc {
namespace node {

using ::node::Buffer;
using v8::Array;
using v8::Date;
using v8::Handle;
using v8::HandleScope;
using v8::Number;
using v8::Object;
using v8::Persistent;
using v8::String;
using v8::Value;

Handle<Value> ParseMetadata(grpc_metadata *metadata_elements, size_t length) {
  NanEscapableScope();
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

Handle<Value> GetEventData(grpc_event *event) {
  NanEscapableScope();
  size_t count;
  grpc_metadata *items;
  Handle<Array> metadata;
  Handle<Object> status;
  Handle<Object> rpc_new;
  switch (event->type) {
    case GRPC_READ:
      return NanEscapeScope(ByteBufferToBuffer(event->data.read));
    case GRPC_INVOKE_ACCEPTED:
      return NanEscapeScope(NanNew<Number>(event->data.invoke_accepted));
    case GRPC_WRITE_ACCEPTED:
      return NanEscapeScope(NanNew<Number>(event->data.write_accepted));
    case GRPC_FINISH_ACCEPTED:
      return NanEscapeScope(NanNew<Number>(event->data.finish_accepted));
    case GRPC_CLIENT_METADATA_READ:
      count = event->data.client_metadata_read.count;
      items = event->data.client_metadata_read.elements;
      return NanEscapeScope(ParseMetadata(items, count));
    case GRPC_FINISHED:
      status = NanNew<Object>();
      status->Set(NanNew("code"), NanNew<Number>(event->data.finished.status));
      if (event->data.finished.details != NULL) {
        status->Set(NanNew("details"),
                    String::New(event->data.finished.details));
      }
      count = event->data.finished.metadata_count;
      items = event->data.finished.metadata_elements;
      status->Set(NanNew("metadata"), ParseMetadata(items, count));
      return NanEscapeScope(status);
    case GRPC_SERVER_RPC_NEW:
      rpc_new = NanNew<Object>();
      if (event->data.server_rpc_new.method == NULL) {
        return NanEscapeScope(NanNull());
      }
      rpc_new->Set(
          NanNew<String, const char *>("method"),
          NanNew<String, const char *>(event->data.server_rpc_new.method));
      rpc_new->Set(
          NanNew<String, const char *>("host"),
          NanNew<String, const char *>(event->data.server_rpc_new.host));
      rpc_new->Set(NanNew<String, const char *>("absolute_deadline"),
                   NanNew<Date>(TimespecToMilliseconds(
                       event->data.server_rpc_new.deadline)));
      count = event->data.server_rpc_new.metadata_count;
      items = event->data.server_rpc_new.metadata_elements;
      metadata = NanNew<Array>(static_cast<int>(count));
      for (unsigned int i = 0; i < count; i++) {
        Handle<Object> item_obj = Object::New();
        item_obj->Set(NanNew<String, const char *>("key"),
                      NanNew<String, char *>(items[i].key));
        item_obj->Set(
            NanNew<String, const char *>("value"),
            NanNew<String, char *>(items[i].value,
                                   static_cast<int>(items[i].value_length)));
        metadata->Set(i, item_obj);
      }
      rpc_new->Set(NanNew("metadata"), ParseMetadata(items, count));
      return NanEscapeScope(rpc_new);
    default:
      return NanEscapeScope(NanNull());
  }
}

Handle<Value> CreateEventObject(grpc_event *event) {
  NanEscapableScope();
  if (event == NULL) {
    return NanEscapeScope(NanNull());
  }
  Handle<Object> event_obj = NanNew<Object>();
  Handle<Value> call;
  if (TagHasCall(event->tag)) {
    call = TagGetCall(event->tag);
  } else {
    call = Call::WrapStruct(event->call);
  }
  event_obj->Set(NanNew<String, const char *>("call"), call);
  event_obj->Set(NanNew<String, const char *>("type"),
                 NanNew<Number>(event->type));
  event_obj->Set(NanNew<String, const char *>("data"), GetEventData(event));

  return NanEscapeScope(event_obj);
}

}  // namespace node
}  // namespace grpc
