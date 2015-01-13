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

using v8::Array;
using v8::Date;
using v8::Handle;
using v8::HandleScope;
using v8::Number;
using v8::Object;
using v8::Persistent;
using v8::String;
using v8::Value;

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
      metadata = NanNew<Array>(static_cast<int>(count));
      for (unsigned int i = 0; i < count; i++) {
        Handle<Object> item_obj = NanNew<Object>();
        item_obj->Set(NanNew<String, const char *>("key"),
                      NanNew<String, char *>(items[i].key));
        item_obj->Set(NanNew<String, const char *>("value"),
                      NanNew<String, char *>(
                          items[i].value,
                          static_cast<int>(items[i].value_length)));
        metadata->Set(i, item_obj);
      }
      return NanEscapeScope(metadata);
    case GRPC_FINISHED:
      status = NanNew<Object>();
      status->Set(NanNew("code"), NanNew<Number>(
          event->data.finished.status));
      if (event->data.finished.details != NULL) {
        status->Set(NanNew("details"), String::New(
            event->data.finished.details));
      }
      count = event->data.finished.metadata_count;
      items = event->data.finished.metadata_elements;
      metadata = NanNew<Array>(static_cast<int>(count));
      for (unsigned int i = 0; i < count; i++) {
        Handle<Object> item_obj = NanNew<Object>();
        item_obj->Set(NanNew<String, const char *>("key"),
                      NanNew<String, char *>(items[i].key));
        item_obj->Set(NanNew<String, const char *>("value"),
                      NanNew<String, char *>(
                          items[i].value,
                          static_cast<int>(items[i].value_length)));
        metadata->Set(i, item_obj);
      }
      status->Set(NanNew("metadata"), metadata);
      return NanEscapeScope(status);
    case GRPC_SERVER_RPC_NEW:
      rpc_new = NanNew<Object>();
      if (event->data.server_rpc_new.method == NULL) {
        return NanEscapeScope(NanNull());
      }
      rpc_new->Set(NanNew<String, const char *>("method"),
                   NanNew<String, const char *>(
                       event->data.server_rpc_new.method));
      rpc_new->Set(NanNew<String, const char *>("host"),
                   NanNew<String, const char *>(
                       event->data.server_rpc_new.host));
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
        item_obj->Set(NanNew<String, const char *>("value"),
                      NanNew<String, char *>(
                          items[i].value,
                          static_cast<int>(items[i].value_length)));
        metadata->Set(i, item_obj);
      }
      rpc_new->Set(NanNew<String, const char *>("metadata"), metadata);
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
