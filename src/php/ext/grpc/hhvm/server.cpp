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
#include "common.h"
#include "server_credentials.h"
#include "timeval.h"

#include "grpc/grpc_security.h"

#include "hphp/runtime/base/req-containers.h"
#include "hphp/runtime/vm/native-data.h"


namespace HPHP {

/*****************************************************************************/
/*                              Server Data                                  */
/*****************************************************************************/

Class* ServerData::s_pClass{ nullptr };
const StaticString ServerData::s_ClassName{ "Grpc\\Server" };

Class* const ServerData::getClass(void)
{
    if (!s_pClass)
    {
        s_pClass = Unit::lookupClass(s_ClassName.get());
        assert(s_pClass);
    }
    return s_pClass;
}

ServerData::ServerData(void) : m_pServer{ nullptr }
{
}

ServerData::~ServerData(void)
{
    destroy();
}

void ServerData::init(grpc_server* const pServer)
{
    // destroy any existing server
    destroy();

    m_pServer = pServer;

    // create completion queue for server
    CompletionQueue::getServerQueue(m_pComletionQueue);
}

void ServerData::destroy(void)
{
    if (m_pServer)
    {
        // shudown server and cancel all calls
        grpc_server_shutdown_and_notify(m_pServer,
                                        m_pComletionQueue->queue(), m_pServer);
        grpc_server_cancel_all_calls(m_pServer);

        // wait for all calls to finish
        for(;;)
        {
            grpc_event event( grpc_completion_queue_next(m_pComletionQueue->queue(),
                                                         gpr_inf_future(GPR_CLOCK_REALTIME), nullptr) );
            if ((event.type == GRPC_OP_COMPLETE) && (event.tag == m_pServer)) break;
        }

        // destroy server no calls can be in progress
        grpc_server_destroy(m_pServer);
        m_pServer = nullptr;
    }
}

/*****************************************************************************/
/*                             HHVM Sever Methods                            */
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
        }

        pServer = grpc_server_create(&channelArgs.args(), nullptr);
    }
    else
    {
        SystemLib::throwInvalidArgumentExceptionObject("channel arguments must be array");
    }

    if (!pServer)
    {
        SystemLib::throwBadMethodCallExceptionObject("failed to create server");
    }
    pServerData->init(pServer);

    grpc_server_register_completion_queue(pServerData->server(),
                                          pServerData->queue()->queue(), nullptr);
}

Object HHVM_METHOD(Server, requestCall)
{
    HHVM_TRACE_SCOPE("Server requestCall") // Degug Trace

    Object resultObj{ SystemLib::AllocStdClassObject() };

    ServerData* const pServerData{ Native::data<ServerData>(this_) };

    typedef struct CallDetails
    {
        CallDetails(void) : metadata{ false }, method_text{ nullptr }, host_text{ nullptr }
        {
            grpc_call_details_init(&details);
        }
        ~CallDetails(void)
        {
            if (method_text)
            {
                gpr_free(method_text);
                method_text = nullptr;
            }
            if (host_text)
            {
                gpr_free(host_text);
                host_text = nullptr;
            }
            grpc_call_details_destroy(&details);
        }
        MetadataArray metadata;    // owned by caller
        grpc_call_details details; // ownerd by caller
        char* method_text;         // owned by caller
        char* host_text;           // owned by caller
    } CallDetails;

    CallDetails callDetails{};
    grpc_call *pCall;
    grpc_call_error errorCode{ grpc_server_request_call(pServerData->server(), &pCall,
                                                        &callDetails.details,
                                                        &callDetails.metadata.array(),
                                                        pServerData->queue()->queue(),
                                                        pServerData->queue()->queue(), nullptr) };

    if (errorCode != GRPC_CALL_OK)
    {
        std::stringstream oSS;
        oSS << "server request call was called incorrectly: " << errorCode << std::endl;
        SystemLib::throwBadMethodCallExceptionObject(oSS.str());
    }

    grpc_event event( grpc_completion_queue_next(pServerData->queue()->queue(),
                                                 gpr_inf_future(GPR_CLOCK_REALTIME), nullptr) );

    if (event.type != GRPC_OP_COMPLETE )
    {
        // return empty object
        return resultObj;
    }

    callDetails.method_text = grpc_slice_to_c_string(callDetails.details.method);
    callDetails.host_text = grpc_slice_to_c_string(callDetails.details.host);
    resultObj.o_set("method_text", String{ callDetails.method_text, CopyString });
    resultObj.o_set("host_text", String{ callDetails.host_text, CopyString });

    Object callObj{ CallData::getClass() };
    CallData* const pCallData{ Native::data<CallData>(callObj) };
    pCallData->init(pCall, false); // server doesn't own call

    Object timevalObj{ TimevalData::getClass() };
    TimevalData* const pTimevalData{ Native::data<TimevalData>(timevalObj) };
    pTimevalData->init(callDetails.details.deadline);

    resultObj.o_set("call", callObj);
    resultObj.o_set("absolute_deadline", timevalObj);
    resultObj.o_set("metadata", callDetails.metadata.phpData());

    return resultObj;
}

bool HHVM_METHOD(Server, addHttp2Port,
                 const String& addr)
{
    HHVM_TRACE_SCOPE("Server addHttp2Port") // Degug Trace

    ServerData* const pServerData{ Native::data<ServerData>(this_) };

    return (grpc_server_add_insecure_http2_port(pServerData->server(), addr.c_str()) != 0);
}

bool HHVM_METHOD(Server, addSecureHttp2Port,
                 const String& addr,
                 const Object& server_credentials)
{
    HHVM_TRACE_SCOPE("Server addSecureHttp2Port") // Degug Trace

    ServerData* const pServerData{ Native::data<ServerData>(this_) };
    ServerCredentialsData* const pServerCredentialsData{ Native::data<ServerCredentialsData>(server_credentials) };

    return (grpc_server_add_secure_http2_port(pServerData->server(),
                                              addr.c_str(), pServerCredentialsData->credentials()) != 0);
}

void HHVM_METHOD(Server, start)
{
    HHVM_TRACE_SCOPE("Server start") // Degug Trace

    ServerData* const pServerData{ Native::data<ServerData>(this_) };
    grpc_server_start(pServerData->server());
}

} // namespace HPHP
