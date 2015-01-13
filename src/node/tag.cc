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
      : persist_tag(tag), persist_call(call) {
  }

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
  return reinterpret_cast<void*>(tag_struct);
}

Handle<Value> GetTagHandle(void *tag) {
  NanEscapableScope();
  struct tag *tag_struct = reinterpret_cast<struct tag*>(tag);
  Handle<Value> tag_value = NanNew<Value>(*tag_struct->persist_tag);
  return NanEscapeScope(tag_value);
}

bool TagHasCall(void *tag) {
  struct tag *tag_struct = reinterpret_cast<struct tag*>(tag);
  return tag_struct->persist_call != NULL;
}

Handle<Value> TagGetCall(void *tag) {
  NanEscapableScope();
  struct tag *tag_struct = reinterpret_cast<struct tag*>(tag);
  if (tag_struct->persist_call == NULL) {
    return NanEscapeScope(NanNull());
  }
  Handle<Value> call_value = NanNew<Value>(*tag_struct->persist_call);
  return NanEscapeScope(call_value);
}

void DestroyTag(void *tag) {
  delete reinterpret_cast<struct tag*>(tag);
}

}  // namespace node
}  // namespace grpc
