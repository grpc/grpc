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

#include "channel_credentials.h"
#include "call_credentials.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "common.h"

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/req-containers.h"
#include "hphp/runtime/base/type-resource.h"
#include "hphp/runtime/base/object-data.h"
#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/builtin-functions.h"

#include "call.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

namespace HPHP {

Class* CallCredentialsData::s_class = nullptr;
const StaticString CallCredentialsData::s_className("Grpc\\Channel");

IMPLEMENT_GET_CLASS(CallCredentialsData);

CallCredentialsData::CallCredentialsData() {}
CallCredentialsData::~CallCredentialsData() { sweep(); }

void CallCredentialsData::init(grpc_call_credentials* call_credentials) {
  wrapped = call_credentials;
}

void CallCredentialsData::sweep() {
  if (wrapped) {
    grpc_call_credentials_release(wrapped);
    wrapped = nullptr;
  }
}

grpc_call_credentials* CallCredentialsData::getWrapped() {
  return wrapped;
}

/**
 * Create composite credentials from two existing credentials.
 * @param CallCredentials $cred1_obj The first credential
 * @param CallCredentials $cred2_obj The second credential
 * @return CallCredentials The new composite credentials object
 */
Object HHVM_STATIC_METHOD(CallCredentials, createComposite,
  const Object& cred1_obj,
  const Object& cred2_obj) {
  auto callCredentialsData1 = Native::data<CallCredentialsData>(cred1_obj);
  auto callCredentialsData2 = Native::data<CallCredentialsData>(cred2_obj);

  grpc_call_credentials *call_credentials =
        grpc_composite_call_credentials_create(callCredentialsData1->getWrapped(),
                                               callCredentialsData2->getWrapped(),
                                               NULL);

  auto newCallCredentialsObj = Object{CallCredentialsData::getClass()};
  auto newCallCredentialsData = Native::data<CallCredentialsData>(newCallCredentialsObj);
  newCallCredentialsData->init(call_credentials);

  return newCallCredentialsObj;
}

/**
 * Create a call credentials object from the plugin API
 * @param function $fci The callback function
 * @return CallCredentials The new call credentials object
 */
Object HHVM_STATIC_METHOD(CallCredentials, createFromPlugin,
  const Variant& function) {

  plugin_state *state;
  state = (plugin_state *)req::calloc(1, sizeof(plugin_state));
  state->function = function;

  grpc_metadata_credentials_plugin plugin;
  plugin.get_metadata = plugin_get_metadata;
  plugin.destroy = plugin_destroy_state;
  plugin.state = (void *)state;
  plugin.type = "";

  auto newCallCredentialsObj = Object{CallCredentialsData::getClass()};
  auto newCallCredentialsData = Native::data<CallCredentialsData>(newCallCredentialsObj);
  newCallCredentialsData->init(grpc_metadata_credentials_create_from_plugin(plugin, NULL));

  return newCallCredentialsObj;
}

void plugin_get_metadata(void *ptr, grpc_auth_metadata_context context,
                         grpc_credentials_plugin_metadata_cb cb,
                         void *user_data) {
  Object returnObj = SystemLib::AllocStdClassObject();
  returnObj.o_set("service_url", Variant(String(context.service_url, CopyString)));
  returnObj.o_set("method_name", Variant(String(context.method_name, CopyString)));

  Array params = Array();
  params.append(returnObj);

  plugin_state *state = (plugin_state *)ptr;

  Variant retval = vm_call_user_func(state->function, params);

  grpc_status_code code = GRPC_STATUS_OK;
  grpc_metadata_array metadata;
  bool cleanup = true;

  if (!retval.isArray()) {
    cleanup = false;
    code = GRPC_STATUS_INVALID_ARGUMENT;
  } else if (!hhvm_create_metadata_array(retval.toArray(), &metadata)) {
    code = GRPC_STATUS_INVALID_ARGUMENT;
  }

  /* Pass control back to core */
  cb(user_data, metadata.metadata, metadata.count, code, NULL);
  if (cleanup) {
    for (int i = 0; i < metadata.count; i++) {
      grpc_slice_unref(metadata.metadata[i].value);
    }
    grpc_metadata_array_destroy(&metadata);
  }
}


void plugin_destroy_state(void *ptr) {
  plugin_state *state = (plugin_state *)ptr;
  req::free(state);
}

} // namespace HPHP
