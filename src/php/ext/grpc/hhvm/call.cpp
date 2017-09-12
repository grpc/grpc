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
#include <future>
#include <mutex>
#include <sstream>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "call.h"
#include "call_credentials.h"
#include "channel.h"
#include "common.h"
#include "completion_queue.h"
#include "timeval.h"

#include "hphp/runtime/base/array-iterator.h"
#include "hphp/runtime/base/memory-manager.h"
#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/vm/vm-regs.h"

#include "grpc/grpc_security.h"
#include "grpc/support/alloc.h"

namespace HPHP {

/*****************************************************************************/
/*                                 Call Data                                 */
/*****************************************************************************/

Class* CallData::s_pClass{ nullptr };
const StaticString CallData::s_ClassName{ "Grpc\\Call" };

Class* const CallData::getClass(void)
{
    if (!s_pClass)
    {
        s_pClass = Unit::lookupClass(s_ClassName.get());
        assert(s_pClass);
    }
    return s_pClass;
}

CallData::CallData(void) : m_pCall{ nullptr }, m_Owned{ false }, m_pCallCredentials{ nullptr },
    m_pChannel{ nullptr }, m_Timeout{ 0 },
    m_pMetadataPromise{ std::make_shared<MetadataPromise>() },
    m_pMetadataMutex{ std::make_shared<std::mutex>() }, m_pCallCancelled{ std::make_shared<bool>(false) },
    m_pSendMessages{ nullptr }
{
}

CallData::CallData(grpc_call* const call, const bool owned, const int32_t timeoutMs) :
    m_pCall{ call }, m_Owned{ owned }, m_pCallCredentials{ nullptr }, m_pChannel{ nullptr },
    m_Timeout{ timeoutMs }, m_pMetadataPromise{ std::make_shared<MetadataPromise>() },
    m_pMetadataMutex{ std::make_shared<std::mutex>() }, m_pCallCancelled{ std::make_shared<bool>(false) },
    m_pSendMessages{ nullptr }
{
}

CallData::~CallData(void)
{
    destroy();
}

void CallData::init(grpc_call* const call, const bool owned, const int32_t timeoutMs)
{
    // destroy any existing call
    destroy();

    m_pCall = call;
    m_Owned = owned;
    m_Timeout = timeoutMs;
}

void CallData::destroy(void)
{
    if (m_pCall)
    {
        // clean up send messages
        if (m_pSendMessages)
        {
            OpsManaged::destroyStatic(m_pSendMessages);
            m_pSendMessages = nullptr;
        }

        if (m_Owned)
        {
            // destroy call
            grpc_call_unref(m_pCall);
            m_Owned = false;
        }

        m_pCall = nullptr;
    }
    m_pChannel = nullptr;
    m_pCallCredentials = nullptr;
}

 void CallData::setQueue(std::unique_ptr<CompletionQueue>&& pCompletionQueue)
{
    m_pCompletionQueue = std::move(pCompletionQueue);
}

/*****************************************************************************/
/*                              Metadata Array                               */
/*****************************************************************************/

MetadataArray::MetadataArray(const bool owned) : m_PHPData{}, m_Owned{ owned }
{
    grpc_metadata_array_init(&m_Array);
    // NOTE: Do not intialize the metadata array here with any members as
    // some metadata is owned by caller and some by callee
}

MetadataArray::~MetadataArray(void)
{
    destroyPHP();
    grpc_metadata_array_destroy(&m_Array);
}

// Populates a grpc_metadata_array with the data in a PHP array object.
// Returns true on success and false on failure
bool MetadataArray::init(const Array& phpArray)
{
    if (!owned())
    {
        SystemLib::throwRuntimeExceptionObject("can only init an owned metadata array");
    }

    // destroy any PHP data
    destroyPHP();
    m_Array.count = 0;

    // precheck validity of data
    size_t count{ 0 };
    for (ArrayIter iter{ phpArray }; iter; ++iter)
    {
        Variant key{ iter.first() };
        if (key.isNull() || !key.isString() ||
            !grpc_header_key_is_legal(grpc_slice_from_static_string(key.toString().c_str())))
        {
            return false;
        }

        Variant value{ iter.second() };
        if (value.isNull() || !value.isArray())
        {
            return false;
        }

        Array innerArray{ value.toArray() };
        for (ArrayIter iter2(innerArray); iter2; ++iter2, ++count)
        {
            Variant value2{ iter2.second() };
            if (value2.isNull() || !value2.isString())
            {
                return false;
            }
        }
    }
    if (count > m_Array.capacity) resizeMetadata(count);

    // create metadata array
    size_t elem{ 0 };
    for (ArrayIter iter(phpArray); iter; ++iter)
    {
        Variant key{ iter.first() };
        Variant value{ iter.second() };
        Array innerArray{ value.toArray() };
        for (ArrayIter iter2(innerArray); iter2; ++iter2, ++elem)
        {
            Variant value2{ iter2.second() };
            String value2Str{ value2.toString() };
            Slice keySlice{ key.toString() };
            Slice valueSlice{ value2Str };
            m_PHPData.emplace_back(std::move(keySlice), std::move(valueSlice));

            m_Array.metadata[elem].key = m_PHPData[elem].first.slice();
            m_Array.metadata[elem].value = m_PHPData[elem].second.slice();
        }
    }
    m_Array.count = count;
    return true;
}

// Creates and returns a PHP array object with the data in a
// grpc_metadata_array. Returns NULL on failure
Variant MetadataArray::phpData(void) const
{
    Array phpArray{ Array::Create() };
    for(size_t elem{ 0 }; elem < m_Array.count; ++elem)
    {
        const grpc_metadata& element(m_Array.metadata[elem]);

        Slice keySlice{ element.key };
        String key{ reinterpret_cast<const char* const>(keySlice.data()), keySlice.length(),
                                                        CopyString };
        Slice valueSlice{ element.value };
        String value{ reinterpret_cast<const char* const>(valueSlice.data()), valueSlice.length(),
                      CopyString };

        if (!phpArray.exists(key, true))
        {
            phpArray.set(key, Array::Create(), true);
        }

        Variant current{ phpArray[key] };
        if (current.isNull() || !current.isArray())
        {
            SystemLib::throwInvalidArgumentExceptionObject("Metadata hash somehow contains wrong types.");
            return Variant{};
        }
        Array currentArray{ current.toArray() };
        currentArray.append(Variant{ value });
    }

    return Variant(phpArray);
}

void MetadataArray::destroyPHP(void)
{
    // destroy PHP data
    m_PHPData.clear();
}

void MetadataArray::resizeMetadata(const size_t capacity)
{
    if (capacity > m_Array.capacity)
    {
        // allocate new memory
        grpc_metadata* const pMetadataNew{ reinterpret_cast<grpc_metadata*>(gpr_zalloc(capacity * sizeof(grpc_metadata))) };

        // move existing items
        for(size_t elem{ 0 }; elem < m_Array.count; ++elem)
        {
            std::memcpy(reinterpret_cast<void*>(&pMetadataNew[elem]),
                        reinterpret_cast<const void*>(&m_Array.metadata[elem]), sizeof(grpc_metadata));
        }


        // destroy old memory
        gpr_free(m_Array.metadata);
        m_Array.metadata = pMetadataNew;
        m_Array.capacity = capacity;
    }
}

/*****************************************************************************/
/*                               OpsManaged                                  */
/*****************************************************************************/


// constructors/destructors
OpsManaged::OpsManaged(void) : recv_metadata{ false }, recv_trailing_metadata{ false },
    recv_status_details{}, send_status_details{},
    cancelled{ 0 }, status{ GRPC_STATUS_OK }
{
    recv_messages.fill(nullptr);
}

OpsManaged::~OpsManaged(void)
{
    destroy();
}

void OpsManaged::destroy(void)
{
    std::for_each(recv_messages.begin(), recv_messages.end(), freeMessage);
}

void OpsManaged::destroyStatic(MessagesType* const pSendMessages)
{
    std::for_each(pSendMessages->begin(), pSendMessages->end(), freeMessage);
}

void OpsManaged::freeMessage(grpc_byte_buffer* pBuffer)
{
    if (pBuffer)
    {
        grpc_byte_buffer_destroy(pBuffer);
        pBuffer = nullptr;
    }
}

// initialize statics
// NOTE: These variables are thread_local so that they have a lifetime beyod startBatch
// where they are set
thread_local MetadataArray OpsManaged::s_Send_metadata{ true };
thread_local MetadataArray OpsManaged::s_Send_trailing_metadata{ true };
thread_local OpsManaged::MessagesType
OpsManaged::s_Send_messages{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

/*****************************************************************************/
/*                              HHVM Call Methods                            */
/*****************************************************************************/

void HHVM_METHOD(Call, __construct,
                 const Object& channel_obj,
                 const String& method,
                 const Object& deadline_obj,
                 const Variant& host_override /* = null */)
{
    HHVM_TRACE_SCOPE("Call construct") // Degug Trace

    CallData* const pCallData{ Native::data<CallData>(this_) };
    ChannelData* const pChannelData{ Native::data<ChannelData>(channel_obj) };

    if (pChannelData->channel() == nullptr)
    {
        SystemLib::throwBadMethodCallExceptionObject("Call cannot be constructed from a closed Channel");
    }
    pCallData->setChannel(pChannelData);

    TimevalData* const pDeadlineTimevalData{ Native::data<TimevalData>(deadline_obj) };

    Slice method_slice{ !method.empty() ? method : String{} };
    Slice host_slice{ !host_override.isNull() && host_override.isString() ?
                       host_override.toString() : String{} };

    std::unique_ptr<CompletionQueue> pCompletionQueue{ nullptr };
    CompletionQueue::getClientQueue(pCompletionQueue);

    grpc_call* const pCall{ grpc_channel_create_call(pChannelData->channel(),
                                                     nullptr, GRPC_PROPAGATE_DEFAULTS,
                                                     pCompletionQueue->queue(),
                                                     method_slice.slice(),
                                                     !host_slice.empty() ? &host_slice.slice() : nullptr,
                                                     pDeadlineTimevalData->time(), nullptr) };

    if (!pCall)
    {
        SystemLib::throwBadMethodCallExceptionObject("failed to create call");
    }

    int32_t timeout{ gpr_time_to_millis(gpr_convert_clock_type(pDeadlineTimevalData->time(),
                                                               GPR_TIMESPAN)) };
    pCallData->init(pCall, true, timeout);
    pCallData->setQueue(std::move(pCompletionQueue));

    return;
}

Object HHVM_METHOD(Call, startBatch,
                   const Array& actions) // array<int, mixed>
{
    VMRegGuard _;

    HHVM_TRACE_SCOPE("Call startBatch") // Degug Trace

    Object resultObj{ SystemLib::AllocStdClassObject() };

    if (actions.size() > OpsManaged::s_MaxActions)
    {
        std::stringstream oSS;
        oSS << "actions array must not be longer than " << OpsManaged::s_MaxActions << " operations" << std::endl;
        SystemLib::throwInvalidArgumentExceptionObject(oSS.str());
    }

    // clear any existing ops data
    static thread_local std::array<grpc_op, OpsManaged::s_MaxActions> ops;
    std::memset(ops.data(), 0, sizeof(grpc_op) * OpsManaged::s_MaxActions);

    OpsManaged opsManaged{};
    CallData* const pCallData{ Native::data<CallData>(this_) };
    pCallData->setSendMessages(&OpsManaged::s_Send_messages);

    size_t op_num{ 0 };
    bool sending_initial_metadata{ false };
    for (ArrayIter iter(actions); iter && (op_num < OpsManaged::s_MaxActions); ++iter, ++op_num)
    {
        Variant key{ iter.first() };
        Variant value{ iter.second() };
        if (key.isNull() || !key.isInteger())
        {
            SystemLib::throwInvalidArgumentExceptionObject("batch keys must be integers");
        }

        int32_t index{ key.toInt32() };
        ops[op_num].flags = 0;
        switch(index)
        {
        case GRPC_OP_SEND_INITIAL_METADATA:
        {
            if (value.isNull() || !value.isArray())
            {
                SystemLib::throwInvalidArgumentExceptionObject("Expected an array value for the metadata");
            }

            if (!OpsManaged::s_Send_metadata.init(value.toArray()))
            {
                SystemLib::throwInvalidArgumentExceptionObject("Bad metadata value given");
            }

            ops[op_num].data.send_initial_metadata.count = OpsManaged::s_Send_metadata.size();
            ops[op_num].data.send_initial_metadata.metadata = OpsManaged::s_Send_metadata.data();
            sending_initial_metadata = true;
            break;
        }
        case GRPC_OP_SEND_MESSAGE:
        {
            if (value.isNull() || !value.isArray())
            {
                SystemLib::throwInvalidArgumentExceptionObject("Expected an array for send message");
            }

            Array messageArr{ value.toArray() };
            if (messageArr.exists(String{ "flags" }, true))
            {
                Variant messageFlags{ messageArr[String{ "flags" }] };
                if (messageFlags.isNull() || !messageFlags.isInteger())
                {
                    SystemLib::throwInvalidArgumentExceptionObject("Expected an int for message flags");
                }
                ops[op_num].flags = messageFlags.toInt32() & GRPC_WRITE_USED_MASK;
            }

            if (messageArr.exists(String{ "message" }, true))
            {
                Variant messageValue{ messageArr[String{ "message" }] };
                if (messageValue.isNull() || !messageValue.isString())
                {
                    SystemLib::throwInvalidArgumentExceptionObject("Expected a string for send message");
                }
                // convert string to byte buffer and store message in managed data
                String messageValueString{ messageValue.toString() };
                const Slice send_message{ messageValueString };
                OpsManaged::s_Send_messages[op_num] = send_message.byteBuffer();
                ops[op_num].data.send_message.send_message = OpsManaged::s_Send_messages[op_num];
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
            }

            Array statusArr{ value.toArray() };
            if (statusArr.exists(String{ "metadata" }, true))
            {
                Variant innerMetadata{ statusArr[String{ "metadata" }] };
                if (innerMetadata.isNull() || !innerMetadata.isArray())
                {
                    SystemLib::throwInvalidArgumentExceptionObject("Expected an array for server status metadata value");
                }

                if (!OpsManaged::s_Send_trailing_metadata.init(innerMetadata.toArray()))
                {
                    SystemLib::throwInvalidArgumentExceptionObject("Bad trailing metadata value given");
                }

                ops[op_num].data.send_status_from_server.trailing_metadata_count = OpsManaged::s_Send_trailing_metadata.size();
                ops[op_num].data.send_status_from_server.trailing_metadata = OpsManaged::s_Send_trailing_metadata.data();
            }

            if (!statusArr.exists(String{ "code" }, true))
            {
                SystemLib::throwInvalidArgumentExceptionObject("Integer status code is required");
            }

            Variant innerCode{ statusArr[String{ "code" }] };
            if (innerCode.isNull() || !innerCode.isInteger())
            {
                SystemLib::throwInvalidArgumentExceptionObject("Status code must be an integer");
            }
            ops[op_num].data.send_status_from_server.status = static_cast<grpc_status_code>(innerCode.toInt32());

            if (!statusArr.exists(String{ "details" }, true))
            {
                SystemLib::throwInvalidArgumentExceptionObject("String status details is required");
            }

            Variant innerDetails{ statusArr[String{ "details" }] };
            if (innerDetails.isNull() || !innerDetails.isString())
            {
                SystemLib::throwInvalidArgumentExceptionObject("Status details must be a string");
            }

            Slice innerDetailsSlice{ innerDetails.toString() };
            opsManaged.send_status_details = std::move(innerDetailsSlice);
            ops[op_num].data.send_status_from_server.status_details = &opsManaged.send_status_details.slice();
            break;
        }
        case GRPC_OP_RECV_INITIAL_METADATA:
            ops[op_num].data.recv_initial_metadata.recv_initial_metadata = &opsManaged.recv_metadata.array();
            break;
        case GRPC_OP_RECV_MESSAGE:
            ops[op_num].data.recv_message.recv_message = &opsManaged.recv_messages[op_num];
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
        }

        ops[op_num].op = static_cast<grpc_op_type>(index);
        ops[op_num].reserved = nullptr;
    }

    // set up the crendential promise for the call credentials set up with this call for
    // the plugin_get_metadata routine
    bool credentialedCall{ sending_initial_metadata && pCallData->credentialed() };
    if (credentialedCall)
    {
        PluginMetadataInfo& pluginMetadataInfo{ PluginMetadataInfo::getPluginMetadataInfo() };
        PluginMetadataInfo::MetaDataInfo metaDataInfo{ pCallData->sharedPromise(),
                                                       pCallData->sharedMutex(),
                                                       pCallData->sharedCancelled(),
                                                       std::this_thread::get_id() };
        pluginMetadataInfo.setInfo(pCallData->callCredentials(), std::move(metaDataInfo));
    }

    bool callFailed{ false };
    grpc_status_code failCode{ GRPC_STATUS_OK };
    auto callFailure = [&callFailed, &credentialedCall, &opsManaged, pCallData](void)
    {
        // clean up any meta data info
        if (credentialedCall)
        {
            PluginMetadataInfo& pluginMetadataInfo{ PluginMetadataInfo::getPluginMetadataInfo() };
            pluginMetadataInfo.deleteInfo(pCallData->callCredentials());
        }
        {
            // set call cancelled shared flag
            std::lock_guard<std::mutex> lock{ pCallData->metadataMutex() };
            pCallData->callCancelled()=true;
        }
        // cancel the call with the server
        grpc_call_cancel_with_status(pCallData->call(), GRPC_STATUS_DEADLINE_EXCEEDED,
                                     "RPC Call Timeout Exceeded", nullptr);
        callFailed = true;
    };

    static std::mutex s_StartBatchMutex;
    {
        std::unique_lock<std::mutex> lock{ s_StartBatchMutex };
        grpc_call_error errorCode{ grpc_call_start_batch(pCallData->call(), ops.data(),
                                                         op_num, pCallData->call(), nullptr) };

        if (errorCode != GRPC_CALL_OK)
        {
            lock.unlock();
            callFailure();
            std::stringstream oSS;
            oSS << "start_batch was called incorrectly: " << errorCode << std::endl;
            SystemLib::throwBadMethodCallExceptionObject(oSS.str());
        }
    }

    grpc_event event (grpc_completion_queue_next(pCallData->queue()->queue(),
                                                 gpr_time_from_millis(pCallData->getTimeout(), GPR_TIMESPAN),
                                                 nullptr));
    if (event.type != GRPC_OP_COMPLETE || event.tag != pCallData->call())
    {
        // failed so clean up and return empty object
        callFailure();
        failCode = GRPC_STATUS_DEADLINE_EXCEEDED;
    }

    // This might look weird but it's required due to the way HHVM works. Each request in HHVM
    // has it's own thread and you cannot run application code on a single request in more than
    // one thread. However gRPC calls call_credentials.cpp:plugin_get_metadata in a different thread
    // in many cases and that violates the thread safety within a request in HHVM and causes segfaults
    // at any reasonable concurrency.
    if (credentialedCall && !callFailed)
    {
        // wait on the plugin_get_metadata to complete
        auto getPluginMetadataFuture = pCallData->metadataPromise().get_future();
        std::future_status status{ getPluginMetadataFuture.wait_for(std::chrono::milliseconds{ pCallData->getTimeout() }) };
        if (status == std::future_status::timeout)
        {
            // failed so clean up and return empty object
            callFailure();
            failCode = GRPC_STATUS_UNAUTHENTICATED;
        }
        else
        {
            plugin_get_metadata_params metaDataParams{ getPluginMetadataFuture.get() };
            if (!metaDataParams.completed)
            {
                // call the plugin in this thread if it wasn't completed already
                plugin_do_get_metadata(metaDataParams.ptr, metaDataParams.context,
                                       metaDataParams.cb, metaDataParams.user_data);
            }
        }
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
            // TODO: Modify this for multiple receive messages most likely an array assoicated with "message"
            if (!opsManaged.recv_messages[i] || callFailed)
            {
                resultObj.o_set("message", Variant());
            }
            else
            {
                Slice slice{ opsManaged.recv_messages[i] };
                resultObj.o_set("message",
                                Variant{ String{ reinterpret_cast<const char*>(slice.data()),
                                                 slice.length(), CopyString } });
            }
            break;
        }
        case GRPC_OP_RECV_STATUS_ON_CLIENT:
        {
            Object recvStatusObj{ SystemLib::AllocStdClassObject() };
            if (callFailed)
            {
                recvStatusObj.o_set("metadata", Variant{});
                recvStatusObj.o_set("code", Variant{ static_cast<int64_t>(failCode) });
                recvStatusObj.o_set("details",Variant{ String{ "Error occured" } });
                resultObj.o_set("status", std::move(Variant{ recvStatusObj }));
            }
            else
            {
                recvStatusObj.o_set("metadata", opsManaged.recv_trailing_metadata.phpData());
                recvStatusObj.o_set("code", Variant{ static_cast<int64_t>(opsManaged.status) });
                recvStatusObj.o_set("details",
                                    Variant{ String{ reinterpret_cast<const char*>(opsManaged.recv_status_details.data()),
                                                     opsManaged.recv_status_details.length(), CopyString } });
                resultObj.o_set("status", std::move(Variant{ recvStatusObj }));
            }
            break;
        }
        case GRPC_OP_RECV_CLOSE_ON_SERVER:
            resultObj.o_set("cancelled", callFailed ? true : (opsManaged.cancelled != 0));
            break;
        default:
            break;
        }
    }

    return resultObj;
}

String HHVM_METHOD(Call, getPeer)
{
    HHVM_TRACE_SCOPE("Call getPeer") // Degug Trace

    CallData* const pCallData{ Native::data<CallData>(this_) };
    return String{ grpc_call_get_peer(pCallData->call()), CopyString };
}

void HHVM_METHOD(Call, cancel)
{
    HHVM_TRACE_SCOPE("Call cancel") // Degug Trace

    CallData* const pCallData{ Native::data<CallData>(this_) };
    grpc_call_cancel(pCallData->call(), nullptr);
}

int64_t HHVM_METHOD(Call, setCredentials,
                    const Object& creds_obj)
{
    HHVM_TRACE_SCOPE("Call setCredentials") // Degug Trace

    CallData* const pCallData{ Native::data<CallData>(this_) };
    CallCredentialsData* const pCallCredentialsData{ Native::data<CallCredentialsData>(creds_obj) };

    grpc_call_error error{ grpc_call_set_credentials(pCallData->call(),
                                                     pCallCredentialsData->credentials()) };

    pCallData->setCallCredentials((error == GRPC_CALL_OK) ? pCallCredentialsData : nullptr);

    return static_cast<int64_t>(error);
}

} // namespace HPHP
