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

#ifndef NET_GRPC_HHVM_GRPC_CALL_CREDENTIALS_H_
#define NET_GRPC_HHVM_GRPC_CALL_CREDENTIALS_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hphp/runtime/ext/extension.h"

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

namespace HPHP {

class CallCredentialsData {
  private:
    grpc_call_credentials* wrapped{nullptr};
  public:
    static Class* s_class;
    static const StaticString s_className;

    static Class* getClass();

    CallCredentialsData();
    ~CallCredentialsData();

    void init(grpc_call_credentials* call_credentials);
    void sweep();
    grpc_call_credentials* getWrapped();
};

typedef struct plugin_state {
  Variant callback;
} plugin_state;

Object HHVM_STATIC_METHOD(CallCredentials, createComposite,
  const Object& cred1_obj,
  const Object& cred2_obj);

Object HHVM_STATIC_METHOD(CallCredentials, createFromPlugin,
  const Variant& callback);

void plugin_get_metadata(void *ptr, grpc_auth_metadata_context context,
                         grpc_credentials_plugin_metadata_cb cb,
                         void *user_data);
void plugin_destroy_state(void *ptr);

}

#endif /* NET_GRPC_HHVM_GRPC_CALL_CREDENTIALS_H_ */
