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

#include <array>
#include <cstdint>
#include <mutex>
#include <sstream>

#include <sys/eventfd.h>
#include <sys/poll.h>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "common.h"

#include "hphp/runtime/vm/native-data.h"

#include "grpc/grpc_security.h"
#include "grpc/support/alloc.h"

#include "call.h"
#include "call_credentials.h"
#include "channel.h"
#include "completion_queue.h"
#include "timeval.h"
#include "utility.h"

namespace HPHP {

Class* CallData::s_Class = nullptr;
const StaticString CallData::s_ClassName{ "Grpc\\Call" };

CallData::~CallData() { sweep(); }

void CallData::sweep(void) {
    if (m_Wrapped)
    {
        if (m_Owned)
        {
            grpc_call_unref(m_Wrapped);
            m_Owned = false;
        }
        m_Wrapped = nullptr;
    }
    if (m_ChannelData)
    {
        m_ChannelData = nullptr;
    }
}

void HHVM_METHOD(Call, __construct,
                 const Object& channel_obj,
                 const String& method,
                 const Object& deadline_obj,
                 const Variant& host_override /* = null */)
{
    std::cout << "Method Call __construct" << std::endl;
    CallData* const pCallData{ Native::data<CallData>(this_) };
    ChannelData* const pChannelData{ Native::data<ChannelData>(channel_obj) };

    if (pChannelData->getWrapped() == nullptr) {
        SystemLib::throwInvalidArgumentExceptionObject("Call cannot be constructed from a closed Channel");
        return;
    }

    pCallData->setChannelData(pChannelData);

    TimevalData* const pDeadlineTimevalData{ Native::data<TimevalData>(deadline_obj) };
    pCallData->setTimeout(gpr_time_to_millis(gpr_convert_clock_type(pDeadlineTimevalData->getWrapped(),
                                             GPR_TIMESPAN)));

    const Slice method_slice{ !method.empty() ? method.c_str() : "" };
    const Slice host_slice{  !host_override.isNull() ? host_override.toString().c_str() : "" };
    pCallData->init(grpc_channel_create_call(pChannelData->getWrapped(), nullptr, GRPC_PROPAGATE_DEFAULTS,
                                             CompletionQueue::tl_obj.get()->getQueue(),
                                             method_slice.slice(),
                                             !host_override.isNull() ? &host_slice.slice() : nullptr,
                                             pDeadlineTimevalData->getWrapped(), nullptr));

    pCallData->setOwned(true);

    return;
}

Object HHVM_METHOD(Call, startBatch,
                   const Array& actions) // array<int, mixed>
{
    Object resultObj{ SystemLib::AllocStdClassObject() };

    const size_t maxActions{ 8 };

    if (actions.size() > maxActions)
    {
        std::stringstream oSS;
        oSS << "actions array must not be longer than " << maxActions << " operations" << std::endl;
        SystemLib::throwInvalidArgumentExceptionObject("oSS.str()");
        return resultObj;
    }

    typedef struct OpsManaged // this structure is managed data for the ops array
    {
        // constructors/destructors
        OpsManaged(void) : metadata{ true }, trailing_metadata{ true }, recv_metadata{ false },
            recv_trailing_metadata{ false }, send_message{ nullptr },
            recv_message{ nullptr }, recv_status_details{}, send_status_details{},
            cancelled{ 0 }, status{ GRPC_STATUS_OK }
        {
        }
        ~OpsManaged(void)
        {
            recycle();
        }

        // interface functions
        void recycle(void)
        {
            // free allocated messages
            if (send_message)
            {
                grpc_byte_buffer_destroy(send_message);
                send_message = nullptr;
            }
            if (recv_message)
            {
                grpc_byte_buffer_destroy(recv_message);
                recv_message = nullptr;
            }
            cancelled = false;
        }

        // managed data
        MetadataArray metadata;                 // owned by caller
        MetadataArray trailing_metadata;        // owned by caller
        MetadataArray recv_metadata;            // owned by call object
        MetadataArray recv_trailing_metadata;   // owned by call object
        grpc_byte_buffer* send_message;         // owned by caller
        grpc_byte_buffer* recv_message;         // owned by caller
        Slice recv_status_details;              // owned by caller
        Slice send_status_details;              // owned by caller
        int cancelled;
        grpc_status_code status;
    } OpsManaged;

    // NOTE:  Come back and look at make ops and opsManaged data thread_local
    std::array<grpc_op, maxActions> ops;
    OpsManaged opsManaged{};

    // clear any existing ops data and recycle managed data
    memset(ops.data(), 0, sizeof(grpc_op) * maxActions);
    //opsManaged.recycle();

    CallData* const pCallData{ Native::data<CallData>(this_) };

    size_t op_num{ 0 };
    bool sending_initial_metadata{ false };
    for (ArrayIter iter(actions); iter; ++iter, ++op_num)
    {
        Variant key{ iter.first() };
        Variant value{ iter.second() };
        if (key.isNull() || !key.isInteger())
        {
            SystemLib::throwInvalidArgumentExceptionObject("batch keys must be integers");
            return resultObj;
        }

        int32_t index{ key.toInt32() };
        switch(index)
        {
        case GRPC_OP_SEND_INITIAL_METADATA:
        {
            if (value.isNull() || !value.isArray())
            {
                SystemLib::throwInvalidArgumentExceptionObject("Expected an array value for the metadata");
                return resultObj;
            }

            if (!opsManaged.metadata.init(value.toArray(), true))
            {
                SystemLib::throwInvalidArgumentExceptionObject("Bad metadata value given");
                return resultObj;
            }

            ops[op_num].data.send_initial_metadata.count = opsManaged.metadata.size();
            ops[op_num].data.send_initial_metadata.metadata = opsManaged.metadata.data();
            sending_initial_metadata = true;
            break;
        }
        case GRPC_OP_SEND_MESSAGE:
        {
            if (value.isNull() || !value.isArray())
            {
                SystemLib::throwInvalidArgumentExceptionObject("Expected an array for send message");
                return resultObj;
            }

            Array messageArr{ value.toArray() };
            if (messageArr.exists(String{ "flags" }, true))
            {
                Variant messageFlags{ messageArr[String{ "flags" }] };
                if (messageFlags.isNull() || !messageFlags.isInteger())
                {
                    SystemLib::throwInvalidArgumentExceptionObject("Expected an int for message flags");
                    return resultObj;
                }
                ops[op_num].flags = messageFlags.toInt32() & GRPC_WRITE_USED_MASK;
            }

            if (messageArr.exists(String{ "message" }, true))
            {
                Variant messageValue{ messageArr[String{ "message" }] };
                if (messageValue.isNull() || !messageValue.isString())
                {
                    SystemLib::throwInvalidArgumentExceptionObject("Expected a string for send message");
                    return resultObj;
                }
                // convert string to byte buffer and store message in managed data
                String messageValueString{ messageValue.toString() };
                const Slice send_message{ messageValueString.c_str(), static_cast<size_t>(messageValueString.size()) };
                opsManaged.send_message = send_message.byteBuffer();
                ops[op_num].data.send_message.send_message = opsManaged.send_message;
            }
            break;
        }
        case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
            break;
        case GRPC_OP_SEND_STATUS_FROM_SERVER:
        {
            if (value.isNull() || !value.isArray())
            {
                SystemLib::throwInvalidArgumentExceptionObject("Expected an array for server status");
                return resultObj;
            }

            Array statusArr{ value.toArray() };
            if (statusArr.exists(String{ "metadata" }, true))
            {
                Variant innerMetadata{ statusArr[String{ "metadata" }] };
                if (innerMetadata.isNull() || !innerMetadata.isArray())
                {
                    SystemLib::throwInvalidArgumentExceptionObject("Expected an array for server status metadata value");
                    return resultObj;
                }

                if (!opsManaged.trailing_metadata.init(innerMetadata.toArray(), true))
                {
                    SystemLib::throwInvalidArgumentExceptionObject("Bad trailing metadata value given");
                    return resultObj;
                }

                ops[op_num].data.send_status_from_server.trailing_metadata_count = opsManaged.trailing_metadata.size();
                ops[op_num].data.send_status_from_server.trailing_metadata = opsManaged.trailing_metadata.data();
            }

            if (!statusArr.exists(String{ "code" }, true))
            {
                SystemLib::throwInvalidArgumentExceptionObject("Integer status code is required");
            }

            Variant innerCode{ statusArr[String{ "code" }] };
            if (innerCode.isNull() || !innerCode.isInteger())
            {
                SystemLib::throwInvalidArgumentExceptionObject("Status code must be an integer");
                return resultObj;
            }
            ops[op_num].data.send_status_from_server.status = static_cast<grpc_status_code>(innerCode.toInt32());

            if (!statusArr.exists(String{ "details" }, true))
            {
                SystemLib::throwInvalidArgumentExceptionObject("String status details is required");
                return resultObj;
            }

            Variant innerDetails{ statusArr[String{ "details" }] };
            if (innerDetails.isNull() || !innerDetails.isString())
            {
                SystemLib::throwInvalidArgumentExceptionObject("Status details must be a string");
                return resultObj;
            }

            Slice innerDetailsSlice{ innerDetails.toString().c_str() };
            opsManaged.send_status_details = innerDetailsSlice;
            ops[op_num].data.send_status_from_server.status_details = &opsManaged.send_status_details.slice();
            break;
        }
        case GRPC_OP_RECV_INITIAL_METADATA:
            ops[op_num].data.recv_initial_metadata.recv_initial_metadata = &opsManaged.recv_metadata.array();
            break;
        case GRPC_OP_RECV_MESSAGE:
            ops[op_num].data.recv_message.recv_message = &opsManaged.recv_message;
            break;
        case GRPC_OP_RECV_STATUS_ON_CLIENT:
            ops[op_num].data.recv_status_on_client.trailing_metadata = &opsManaged.recv_trailing_metadata.array();
            ops[op_num].data.recv_status_on_client.status = &opsManaged.status;
            ops[op_num].data.recv_status_on_client.status_details = &opsManaged.recv_status_details.slice();
            break;
        case GRPC_OP_RECV_CLOSE_ON_SERVER:
            ops[op_num].data.recv_close_on_server.cancelled = &opsManaged.cancelled;
            break;
        default:
            SystemLib::throwInvalidArgumentExceptionObject("Unrecognized key in batch");
            return resultObj;
        }

        ops[op_num].op = static_cast<grpc_op_type>(index);
        ops[op_num].flags = 0;
        ops[op_num].reserved = nullptr;
    }

    grpc_call_error errorCode;
    static std::mutex s_WriteStartBatchMutex, s_ReadStartBatchMutex;
    {
        // TODO : Update for read and write locks for efficiency
        std::lock_guard<std::mutex> lock{ s_WriteStartBatchMutex };
        errorCode = grpc_call_start_batch(pCallData->getWrapped(), ops.data(), op_num, pCallData->getWrapped(), NULL);
    }

    if (errorCode != GRPC_CALL_OK)
    {
        std::stringstream oSS;
        oSS << "start_batch was called incorrectly: " << errorCode << std::endl;
        SystemLib::throwInvalidArgumentExceptionObject("oSS.str()");
        return resultObj;
    }

    // This might look weird but it's required due to the way HHVM works. Each request in HHVM
    // has it's own thread and you cannot run application code on a single request in more than
    // one thread. However gRPC calls call_credentials.cpp:plugin_get_metadata in a different thread
    // in many cases and that violates the thread safety within a request in HHVM and causes segfaults
    // at any reasonable concurrency.
    // TODO: See if this is necessary and if so use condition variable
    if (   sending_initial_metadata == true)
    {
    }

    grpc_event event{ grpc_completion_queue_pluck(CompletionQueue::tl_obj.get()->getQueue(),
                                                  pCallData->getWrapped(),
                                                  gpr_inf_future(GPR_CLOCK_REALTIME), nullptr) };
    // An error occured if success = 0
    if (event.success == 0)
    {
        std::stringstream oSS;
        oSS << "There was a problem with the request. Event error code: " << event.type << std::endl;
        SystemLib::throwRuntimeExceptionObject(oSS.str());
        return resultObj;
    }

    // process results of call
    for (size_t i{ 0 }; i < op_num; ++i)
    {
        switch(ops[i].op)
        {
        case GRPC_OP_SEND_INITIAL_METADATA:
            resultObj.o_set("send_metadata", Variant{ true });
            break;
        case GRPC_OP_SEND_MESSAGE:
            resultObj.o_set("send_message", Variant{ true });
            break;
        case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
            resultObj.o_set("send_close", Variant{ true });
            break;
        case GRPC_OP_SEND_STATUS_FROM_SERVER:
            resultObj.o_set("send_status", Variant{ true });
            break;
        case GRPC_OP_RECV_INITIAL_METADATA:
            resultObj.o_set("metadata", opsManaged.recv_metadata.phpData());
            break;
        case GRPC_OP_RECV_MESSAGE:
        {
            if (!opsManaged.recv_message)
            {
                resultObj.o_set("message", Variant());
            }
            else
            {
                Slice slice{ opsManaged.recv_message };
                resultObj.o_set("message",
                                Variant{ String{ reinterpret_cast<const char*>(slice.data()),
                                                 slice.length(), CopyString } });
            }
            break;
        }
        case GRPC_OP_RECV_STATUS_ON_CLIENT:
        {
            Object recvStatusObj{ SystemLib::AllocStdClassObject() };
            recvStatusObj.o_set("metadata", opsManaged.recv_trailing_metadata.phpData());
            recvStatusObj.o_set("code", Variant{ static_cast<int64_t>(opsManaged.status) });
            recvStatusObj.o_set("details",
                                Variant{ String{ reinterpret_cast<const char*>(opsManaged.recv_status_details.data()),
                                                 opsManaged.recv_status_details.length(), CopyString } });
            resultObj.o_set("status", Variant{ recvStatusObj });
            break;
        }
        case GRPC_OP_RECV_CLOSE_ON_SERVER:
            resultObj.o_set("cancelled", opsManaged.cancelled != 0);
            break;
        default:
            break;
        }
    }

    return resultObj;
}

String HHVM_METHOD(Call, getPeer)
{
    CallData* const pCallData{ Native::data<CallData>(this_) };
    return String{ grpc_call_get_peer(pCallData->getWrapped()), CopyString };
}

void HHVM_METHOD(Call, cancel)
{
    CallData* const pCallData{ Native::data<CallData>(this_) };
    grpc_call_cancel(pCallData->getWrapped(), NULL);
}

int64_t HHVM_METHOD(Call, setCredentials,
                    const Object& creds_obj)
{
    CallData* const pCallData{ Native::data<CallData>(this_) };
    CallCredentialsData* const pCallCredentialsData{ Native::data<CallCredentialsData>(creds_obj) };

    grpc_call_error error{ grpc_call_set_credentials(pCallData->getWrapped(),
                                                     pCallCredentialsData->getWrapped()) };
    return static_cast<int64_t>(error);
}

} // namespace HPHP
