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


#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "server.h"
#include "call.h"
#include "channel.h"
#include "completion_queue.h"
#include "server_credentials.h"
#include "timeval.h"
#include "utility.h"

#include <grpc/grpc_security.h>

#include "hphp/runtime/vm/native-data.h"

#include "hphp/runtime/base/req-containers.h"

//#include "hphp/runtime/base/builtin-functions.h"

namespace HPHP {

/*****************************************************************************/
/*                               ServerData                                 */
/*****************************************************************************/

Class* ServerData::s_Class{ nullptr };
const StaticString ServerData::s_ClassName{ "Grpc\\Server" };

ServerData::~ServerData(void)
{
    destroy();
}

void ServerData::destroy(void)
{
    if (m_pServer) {
        grpc_server_shutdown_and_notify(m_pServer,
                                        CompletionQueue::getQueue().queue(),
                                        nullptr);
        grpc_server_cancel_all_calls(m_pServer);
        grpc_completion_queue_pluck(CompletionQueue::getQueue().queue(), nullptr,
                                    gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
        grpc_server_destroy(m_pServer);
        m_pServer = nullptr;
    }
}

/*****************************************************************************/
/*                               HHVM Methods                                */
/*****************************************************************************/

void HHVM_METHOD(Server, __construct,
                 const Variant& args_array_or_null /* = null */)
{
    HHVM_TRACE_SCOPE("Server construct") // Degug Trace

    ServerData* const pServerData{ Native::data<ServerData>(this_) };
    grpc_server* pServer{ nullptr };
    if (args_array_or_null.isNull())
    {
        pServer = grpc_server_create(nullptr, nullptr);

    }
    else if (args_array_or_null.isArray())
    {
        ChannelArgs channelArgs{};
        if (!channelArgs.init(args_array_or_null.toArray()))
        {
            SystemLib::throwInvalidArgumentExceptionObject("invalid channel arguments");
            return;
        }

        pServer = grpc_server_create(&channelArgs.args(), nullptr);
    }
    else
    {
        SystemLib::throwInvalidArgumentExceptionObject("channel arguments must be array");
        return;
    }

    if (!pServer)
    {
        SystemLib::throwBadMethodCallExceptionObject("failed to create server");
        return;
    }
    pServerData->init(pServer);

    grpc_server_register_completion_queue(pServerData->getWrapped(),
                                          CompletionQueue::getQueue().queue(), nullptr);
}

Object HHVM_METHOD(Server, requestCall)
{
    HHVM_TRACE_SCOPE("Server requestCall") // Degug Trace

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
                                        CompletionQueue::getQueue().queue(),
                                        CompletionQueue::getQueue().queue(), nullptr);

    if (error_code != GRPC_CALL_OK) {
        //SystemLib::throwInvalidArgumentExceptionObject("request_call failed: %d", error_code);
     goto cleanup;
    }

    event = grpc_completion_queue_pluck(CompletionQueue::getQueue().queue(), nullptr,
                                        gpr_inf_future(GPR_CLOCK_REALTIME),
                                        nullptr);

    if (!event.success) {
    SystemLib::throwInvalidArgumentExceptionObject("Failed to request a call for some reason");
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
                 const String& addr)
{
    HHVM_TRACE_SCOPE("Server addHttp2Port") // Degug Trace


  auto serverData = Native::data<ServerData>(this_);
  return (bool)grpc_server_add_insecure_http2_port(serverData->getWrapped(), addr.c_str());
}

bool HHVM_METHOD(Server, addSecureHttp2Port,
                 const String& addr,
                 const Object& server_credentials)
{
    HHVM_TRACE_SCOPE("Server addSecureHttp2Port") // Degug Trace

  auto serverData = Native::data<ServerData>(this_);
  auto serverCredentialsData = Native::data<ServerCredentialsData>(server_credentials);

  return (bool)grpc_server_add_secure_http2_port(serverData->getWrapped(), addr.c_str(), serverCredentialsData->getWrapped());
}

void HHVM_METHOD(Server, start)
{
    HHVM_TRACE_SCOPE("Server start") // Degug Trace

auto serverData = Native::data<ServerData>(this_);
  grpc_server_start(serverData->getWrapped());
}

} // namespace HPHP
