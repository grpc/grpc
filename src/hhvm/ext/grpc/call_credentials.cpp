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
