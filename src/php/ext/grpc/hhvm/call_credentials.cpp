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

 #include <sys/eventfd.h>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "call_credentials.h"
#include "channel_credentials.h"
#include "common.h"
#include "call.h"

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/req-containers.h"
#include "hphp/runtime/base/type-resource.h"
#include "hphp/runtime/base/object-data.h"
#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/builtin-functions.h"
#include "hphp/runtime/base/array-init.h"
#include "hphp/util/process.h"


#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>

#include "common.h"

namespace HPHP {

Class* CallCredentialsData::s_class = nullptr;
const StaticString CallCredentialsData::s_className("Grpc\\CallCredentials");

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

IMPLEMENT_THREAD_LOCAL(PluginGetMetadataFd, PluginGetMetadataFd::tl_obj);

PluginGetMetadataFd::PluginGetMetadataFd() {}
void PluginGetMetadataFd::setFd(int fd_) { fd = fd_; }
int PluginGetMetadataFd::getFd() { return fd; }

/**
 * Create composite credentials from two existing credentials.
 * @param CallCredentials $cred1_obj The first credential
 * @param CallCredentials $cred2_obj The second credential
 * @return CallCredentials The new composite credentials object
 */
Object HHVM_STATIC_METHOD(CallCredentials, createComposite,
                          const Object& cred1_obj,
                          const Object& cred2_obj)
{
    HHVM_TRACE_SCOPE("CallCredentials createComposite") // Degug Trace

  auto callCredentialsData1 = Native::data<CallCredentialsData>(cred1_obj);
  auto callCredentialsData2 = Native::data<CallCredentialsData>(cred2_obj);

  grpc_call_credentials *call_credentials =
        grpc_composite_call_credentials_create(callCredentialsData1->getWrapped(),
                                               callCredentialsData2->getWrapped(),
                                               nullptr);

  auto newCallCredentialsObj = Object{CallCredentialsData::getClass()};
  auto newCallCredentialsData = Native::data<CallCredentialsData>(newCallCredentialsObj);
  newCallCredentialsData->init(call_credentials);

  return newCallCredentialsObj;
}

/**
 * Create a call credentials object from the plugin API
 * @param callable $fci The callback
 * @return CallCredentials The new call credentials object
 */
Object HHVM_STATIC_METHOD(CallCredentials, createFromPlugin,
                          const Variant& callback)
{
    HHVM_TRACE_SCOPE("CallCredentials createFromPlugin") // Degug Trace

  if (!is_callable(callback)) {
    throw_invalid_argument("Callback argument is not a valid callback");
  }

  plugin_state *state;
  state = (plugin_state *)gpr_zalloc(sizeof(plugin_state));
  state->callback = callback;
  state->fd_obj = PluginGetMetadataFd::tl_obj.get();

  grpc_metadata_credentials_plugin plugin;
  plugin.get_metadata = plugin_get_metadata;
  plugin.destroy = plugin_destroy_state;
  plugin.state = (void *)state;
  plugin.type = "";

  auto newCallCredentialsObj = Object{CallCredentialsData::getClass()};
  auto newCallCredentialsData = Native::data<CallCredentialsData>(newCallCredentialsObj);
  newCallCredentialsData->init(grpc_metadata_credentials_create_from_plugin(plugin, nullptr));

  return newCallCredentialsObj;
}

// This work done in this function MUST be done on the same thread as the HHVM request
void plugin_do_get_metadata(void *ptr, grpc_auth_metadata_context context,
                             grpc_credentials_plugin_metadata_cb cb,
                             void *user_data)
{
    HHVM_TRACE_SCOPE("CallCredentials plugin_do_get_metadata") // Degug Trace

  Object returnObj = SystemLib::AllocStdClassObject();
  returnObj.o_set("service_url", String(context.service_url, CopyString));
  returnObj.o_set("method_name", String(context.method_name, CopyString));

  plugin_state *state = (plugin_state *)ptr;

  Variant retval = vm_call_user_func(state->callback, make_packed_array(returnObj));
  if (!retval.isArray()) {
    throw_invalid_argument("Callback return value expected an array.");
    return;
  }

  grpc_status_code code = GRPC_STATUS_OK;

  MetadataArray metadata;

  if (!metadata.init(retval.toArray(), true))
  {
    code = GRPC_STATUS_INVALID_ARGUMENT;
  }

  /* Pass control back to core */
  cb(user_data, metadata.data(), metadata.size(), code, nullptr);
}

void plugin_get_metadata(void *ptr, grpc_auth_metadata_context context,
                         grpc_credentials_plugin_metadata_cb cb,
                         void *user_data)
{
    HHVM_TRACE_SCOPE("CallCredentials plugin_get_metadata") // Degug Trace

  plugin_state *state = (plugin_state *)ptr;
  PluginGetMetadataFd *fd_obj = state->fd_obj;

  plugin_get_metadata_params *params = (plugin_get_metadata_params *)gpr_zalloc(sizeof(plugin_get_metadata_params));
  params->ptr = ptr;
  params->context = context;
  params->cb = cb;
  params->user_data = user_data;

  write(fd_obj->getFd(), &params, sizeof(plugin_get_metadata_params *));
}


void plugin_destroy_state(void *ptr)
{
    HHVM_TRACE_SCOPE("CallCredentials plugin_destroy_state") // Degug Trace

  plugin_state *state = (plugin_state *)ptr;
  gpr_free(state);
}

} // namespace HPHP
