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

#ifndef GRPC_NODE_CALL_CREDENTIALS_H_
#define GRPC_NODE_CALL_CREDENTIALS_H_

#include <queue>

#include <node.h>
#include <nan.h>
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
  std::queue<plugin_callback_data*> *pending_callbacks;
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
