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

#include "call.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "server.h"

#include <stdbool.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>

#include "completion_queue.h"
#include "server.h"
#include "channel.h"
#include "server_credentials.h"
#include "timeval.h"

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/req-containers.h"
#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/builtin-functions.h"

#include "utility.h"

namespace HPHP {

Class* ServerData::s_class = nullptr;
const StaticString ServerData::s_className("Grpc\\Server");

IMPLEMENT_GET_CLASS(ServerData);

ServerData::ServerData() {}
ServerData::~ServerData() { sweep(); }

void ServerData::init(grpc_server* server) {
  wrapped = server;
}

void ServerData::sweep() {
  if (wrapped) {
    grpc_server_shutdown_and_notify(wrapped, CompletionQueue::tl_obj.get()->getQueue(), NULL);
    grpc_server_cancel_all_calls(wrapped);
    grpc_completion_queue_pluck(CompletionQueue::tl_obj.get()->getQueue(), NULL,
                                gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    grpc_server_destroy(wrapped);
    wrapped = nullptr;
  }
}

grpc_server* ServerData::getWrapped() {
  return wrapped;
}

void HHVM_METHOD(Server, __construct,
  const Variant& args_array_or_null /* = null */) {
  auto serverData = Native::data<ServerData>(this_);
  if (args_array_or_null.isNull()) {
    serverData->init(grpc_server_create(NULL, NULL));
  } else {
    grpc_channel_args args;
    if (hhvm_grpc_read_args_array(args_array_or_null.toArray(), &args) == -1) {
      req::free(args.args);
      return;
    }
    serverData->init(grpc_server_create(&args, NULL));
    req::free(args.args);
  }

  grpc_server_register_completion_queue(serverData->getWrapped(), CompletionQueue::tl_obj.get()->getQueue(), NULL);
}

Object HHVM_METHOD(Server, requestCall) {
  char *method_text;
  char *host_text;
  Object callObj;
  CallData *callData;
  Object timevalObj;
  TimevalData *timevalData;

  grpc_call_error error_code;
  grpc_call *call;
  grpc_call_details details;
  MetadataArray metadata;
  grpc_event event;
  Object resultObj = SystemLib::AllocStdClassObject();;

  auto serverData = Native::data<ServerData>(this_);

  grpc_call_details_init(&details);
  error_code = grpc_server_request_call(serverData->getWrapped(), &call, &details, &metadata.array(),
                                 CompletionQueue::tl_obj.get()->getQueue(), CompletionQueue::tl_obj.get()->getQueue(), NULL);

  if (error_code != GRPC_CALL_OK) {
    throw_invalid_argument("request_call failed: %d", error_code);
    goto cleanup;
  }

  event = grpc_completion_queue_pluck(CompletionQueue::tl_obj.get()->getQueue(), NULL,
                                        gpr_inf_future(GPR_CLOCK_REALTIME),
                                        NULL);

  if (!event.success) {
    throw_invalid_argument("Failed to request a call for some reason");
    goto cleanup;
  }

  method_text = grpc_slice_to_c_string(details.method);
  host_text = grpc_slice_to_c_string(details.host);

  resultObj.o_set("method_text", String(method_text, CopyString));
  resultObj.o_set("host_text", String(host_text, CopyString));

  gpr_free(method_text);
  gpr_free(host_text);

  callObj = Object{CallData::getClass()};
  callData = Native::data<CallData>(callObj);
  callData->init(call);

  timevalObj = Object{TimevalData::getClass()};
  timevalData = Native::data<TimevalData>(timevalObj);
  timevalData->init(details.deadline);

  resultObj.o_set("call", callObj);
  resultObj.o_set("absolute_deadline", timevalObj);
  resultObj.o_set("metadata", metadata.phpData());

cleanup:
    grpc_call_details_destroy(&details);
    return resultObj;
}

bool HHVM_METHOD(Server, addHttp2Port,
  const String& addr) {
  auto serverData = Native::data<ServerData>(this_);
  return (bool)grpc_server_add_insecure_http2_port(serverData->getWrapped(), addr.c_str());
}

bool HHVM_METHOD(Server, addSecureHttp2Port,
  const String& addr,
  const Object& server_credentials) {
  auto serverData = Native::data<ServerData>(this_);
  auto serverCredentialsData = Native::data<ServerCredentialsData>(server_credentials);
  return (bool)grpc_server_add_secure_http2_port(serverData->getWrapped(), addr.c_str(), serverCredentialsData->getWrapped());
}

void HHVM_METHOD(Server, start) {
  auto serverData = Native::data<ServerData>(this_);
  grpc_server_start(serverData->getWrapped());
}

} // namespace HPHP
