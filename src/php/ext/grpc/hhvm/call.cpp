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

#include "grpc/grpc_security.h"
#include "grpc/support/alloc.h"

namespace HPHP {

/*****************************************************************************/
/*                                  CallData                                 */
/*****************************************************************************/

Class* CallData::s_Class{ nullptr };
const StaticString CallData::s_ClassName{ "Grpc\\Call" };

CallData::~CallData(void)
{
    destroy();
}

Class* const CallData::getClass(void)
{
    if (!s_Class)
    {
        s_Class = Unit::lookupClass(s_ClassName.get());
        assert(s_Class);
    }
    return s_Class;
}

void CallData::destroy(void)
{
    if (m_pCall)
    {
        if (m_Owned)
        {
            grpc_call_unref(m_pCall);
            m_Owned = false;
        }
        m_pCall = nullptr;
    }
    if (m_ChannelData)
    {
        m_ChannelData = nullptr;
    }
}

/*****************************************************************************/
/*                               MetadataArray                               */
/*****************************************************************************/

MetadataArray::MetadataArray(const bool ownPHP) : m_OwnPHP{ ownPHP },
    m_PHPData{}
{
    grpc_metadata_array_init(&m_Array);
    resizeMetadata(1);
}

MetadataArray::~MetadataArray(void)
{
    m_OwnPHP ? destroyPHP() : freePHP();
    grpc_metadata_array_destroy(&m_Array);
}

// Populates a grpc_metadata_array with the data in a PHP array object.
// Returns true on success and false on failure
bool MetadataArray::init(const Array& phpArray, const bool ownPHP)
{
    // destroy/free any PHP data
    m_OwnPHP ? destroyPHP() : freePHP();
    m_OwnPHP = ownPHP;
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
    m_PHPData.resize(count);
    for (ArrayIter iter(phpArray); iter; ++iter)
    {
        Variant key{ iter.first() };
        Variant value{ iter.second() };
        Array innerArray{ value.toArray() };
        for (ArrayIter iter2(innerArray); iter2; ++iter2, ++elem)
        {
            Variant value2{ iter2.second() };
            String value2Str{ value2.toString() };
            Slice keySlice{ key.toString().c_str() };
            Slice valueSlice{ value2Str.c_str(), static_cast<size_t>(value2Str.size()) };
            m_PHPData[elem] = std::move(std::make_pair(keySlice, valueSlice));

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

        class CopySlice
        {
        public:
            CopySlice(const gpr_slice& slice) : m_Length{ GRPC_SLICE_LENGTH(slice) },
                m_pSlice{ reinterpret_cast<char*>(req::calloc(m_Length + 1, sizeof(char))) }
            {
                std::memcpy(m_pSlice, GRPC_SLICE_START_PTR(slice), m_Length);
            }
            ~CopySlice(void)
            {
                if (m_pSlice)
                {
                    req::free(m_pSlice);
                    m_pSlice = nullptr;
                }
            }
            const char* const slice(void) const { return m_pSlice; }
            size_t length(void) const { return m_Length; }
        private:
            size_t m_Length;
            char* m_pSlice;
        };
        CopySlice keySlice{ element.key };
        CopySlice valueSlice{ element.value };
        String key{ keySlice.slice(), keySlice.length(), CopyString };
        String value{ valueSlice.slice(), valueSlice.length(), CopyString };

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
    m_Array.count = 0;
}

void MetadataArray::freePHP(void)
{
    // free the PHP data by increasinf ref counts
    for(std::pair<Slice, Slice>& phpData : m_PHPData)
    {
        phpData.first.increaseRef();
        phpData.second.increaseRef();
    }

    destroyPHP();
}

void MetadataArray::resizeMetadata(const size_t capacity)
{
    if (capacity > m_Array.capacity)
    {
        // allocate new memory
        grpc_metadata* const pMetadataNew{ reinterpret_cast<grpc_metadata*>(gpr_malloc(capacity * sizeof(grpc_metadata))) };

        // move existing items
        for(size_t elem{ 0 }; elem < m_Array.count; ++elem)
        {
            pMetadataNew[elem] = m_Array.metadata[elem];
        }

        // destroy old memory
        gpr_free(m_Array.metadata);
        m_Array.metadata = pMetadataNew;
        m_Array.capacity = capacity;
    }
}

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
        return;
    }
    pCallData->setChannelData(pChannelData);

    TimevalData* const pDeadlineTimevalData{ Native::data<TimevalData>(deadline_obj) };
    pCallData->setTimeout(gpr_time_to_millis(gpr_convert_clock_type(pDeadlineTimevalData->time(),
                                             GPR_TIMESPAN)));

    Slice method_slice{ !method.empty() ? method.c_str() : nullptr };
    Slice host_slice{ !host_override.isNull() && host_override.isString() ?
                       host_override.toString().c_str() : nullptr };

    grpc_call* const pCall{ grpc_channel_create_call(pChannelData->channel(),
                                                     nullptr, GRPC_PROPAGATE_DEFAULTS,
                                                     CompletionQueue::getClientQueue().queue(),
                                                     method_slice.slice(),
                                                     !host_slice.empty() ? &host_slice.slice() : nullptr,
                                                     pDeadlineTimevalData->time(), nullptr) };

    if (!pCall)
    {
        SystemLib::throwBadMethodCallExceptionObject("failed to create call");
        return;
    }

    pCallData->init(pCall);
    pCallData->setOwned(true);

    return;
}

Object HHVM_METHOD(Call, startBatch,
                   const Array& actions) // array<int, mixed>
{
    HHVM_TRACE_SCOPE("Call startBatch") // Degug Trace

    Object resultObj{ SystemLib::AllocStdClassObject() };

    const size_t maxActions{ 8 };

    if (actions.size() > maxActions)
    {
        std::stringstream oSS;
        oSS << "actions array must not be longer than " << maxActions << " operations" << std::endl;
        SystemLib::throwInvalidArgumentExceptionObject(oSS.str());
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
    std::memset(ops.data(), 0, sizeof(grpc_op) * maxActions);
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
        ops[op_num].flags = 0;
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
                const Slice send_message{ messageValueString.c_str(),
                                          static_cast<size_t>(messageValueString.size()) };
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
                return resultObj;
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
        ops[op_num].reserved = nullptr;
    }

    if (sending_initial_metadata)
    {
        // We do this above grpc_call_start_batch because it can also sometimes execute the callback
        // from within itself and we need to have the promise setup by that point;
        PluginGetMetadataPromise::GetPluginMetadataPromise().setPromise(&pCallData->getPromise());
    }

    static std::mutex s_WriteStartBatchMutex, s_ReadStartBatchMutex;
    {
        // TODO : Update for read and write locks for efficiency
        std::unique_lock<std::mutex> lock{ s_WriteStartBatchMutex };
        grpc_call_error errorCode{ grpc_call_start_batch(pCallData->call(), ops.data(),
                                                         op_num, pCallData->call(), nullptr) };

        if (errorCode != GRPC_CALL_OK)
        {
            lock.unlock();
            std::stringstream oSS;
            oSS << "start_batch was called incorrectly: " << errorCode << std::endl;
            SystemLib::throwBadMethodCallExceptionObject(oSS.str());
            return resultObj;
        }
    }

    grpc_event event( grpc_completion_queue_pluck(CompletionQueue::getClientQueue().queue(),
                                                  pCallData->call(),
                                                  gpr_inf_future(GPR_CLOCK_REALTIME), nullptr) );
    if ((event.success == 0) || (event.type != GRPC_OP_COMPLETE))
    {
        // Don't do anything in the error event. It will be taken care of in the return object
        /*std::stringstream oSS;
        oSS << "There was a problem with the request. Event success code: " << event.success
            << " Type: " << event.type << std::endl;
        SystemLib::throwBadMethodCallExceptionObject(oSS.str());*/
    }
    else
    {
      // This might look weird but it's required due to the way HHVM works. Each request in HHVM
      // has it's own thread and you cannot run application code on a single request in more than
      // one thread. However gRPC calls call_credentials.cpp:plugin_get_metadata in a different thread
      // in many cases and that violates the thread safety within a request in HHVM and causes segfaults
      // at any reasonable concurrency.
      // TODO: See if this is necessary and if so use condition variable
      if (sending_initial_metadata)
      {
          auto getPluginMetadataFuture = pCallData->getPromise().get_future();
          std::future_status status{ getPluginMetadataFuture.wait_for(std::chrono::milliseconds{ pCallData->getTimeout() }) };
          if (status == std::future_status::timeout)
          {
              SystemLib::throwBadMethodCallExceptionObject("There was a problem with the request it timed out");
          }
          else
          {
              plugin_get_metadata_params* const pMetadataParams{ getPluginMetadataFuture.get() };
              if (pMetadataParams != nullptr)
              {
                plugin_do_get_metadata(pMetadataParams->ptr, pMetadataParams->context, pMetadataParams->cb,
                                      pMetadataParams->user_data);
                /*if (pMetadataParams)*/ gpr_free(pMetadataParams);
              }
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
                                                     pCallCredentialsData->getWrapped()) };

    return static_cast<int64_t>(error);
}

} // namespace HPHP
