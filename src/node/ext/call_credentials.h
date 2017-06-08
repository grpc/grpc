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

#ifndef GRPC_NODE_CALL_CREDENTIALS_H_
#define GRPC_NODE_CALL_CREDENTIALS_H_

#include <queue>

#include <nan.h>
#include <node.h>
#include <uv.h>
#include "grpc/grpc_security.h"

namespace grpc {
namespace node {

class CallCredentials : public Nan::ObjectWrap {
 public:
  static void Init(v8::Local<v8::Object> exports);
  static bool HasInstance(v8::Local<v8::Value> val);
  /* Wrap a grpc_call_credentials struct in a javascript object */
  static v8::Local<v8::Value> WrapStruct(grpc_call_credentials *credentials);

  /* Returns the grpc_call_credentials struct that this object wraps */
  grpc_call_credentials *GetWrappedCredentials();

 private:
  explicit CallCredentials(grpc_call_credentials *credentials);
  ~CallCredentials();

  // Prevent copying
  CallCredentials(const CallCredentials &);
  CallCredentials &operator=(const CallCredentials &);

  static NAN_METHOD(New);
  static NAN_METHOD(CreateSsl);
  static NAN_METHOD(CreateFromPlugin);

  static NAN_METHOD(Compose);
  static Nan::Callback *constructor;
  // Used for typechecking instances of this javascript class
  static Nan::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_call_credentials *wrapped_credentials;
};

/* Auth metadata plugin functionality */

typedef struct plugin_callback_data {
  const char *service_url;
  grpc_credentials_plugin_metadata_cb cb;
  void *user_data;
} plugin_callback_data;

typedef struct plugin_state {
  Nan::Callback *callback;
  std::queue<plugin_callback_data *> *pending_callbacks;
  uv_mutex_t plugin_mutex;
  // async.data == this
  uv_async_t plugin_async;
} plugin_state;

void plugin_get_metadata(void *state, grpc_auth_metadata_context context,
                         grpc_credentials_plugin_metadata_cb cb,
                         void *user_data);

void plugin_destroy_state(void *state);

NAN_METHOD(PluginCallback);

NAUV_WORK_CB(SendPluginCallback);

}  // namespace node
}  // namepsace grpc

#endif  // GRPC_NODE_CALL_CREDENTIALS_H_
