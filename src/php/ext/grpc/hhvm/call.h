
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

#ifndef NET_GRPC_HHVM_GRPC_CALL_H_
#define NET_GRPC_HHVM_GRPC_CALL_H_

#include <cstdint>
#include <memory>
#include <future>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "call_credentials.h"
#include "slice.h"

#include "hphp/runtime/ext/extension.h"

#include "grpc/grpc.h"
#include "grpc/support/alloc.h"

namespace HPHP {

/*****************************************************************************/
/*                             Metadata Array                                */
/*****************************************************************************/

// This class is an RAII wrapper around a call metadata array
class MetadataArray
{
public:
    // constructors/descructors
    MetadataArray(const bool owned);
    ~MetadataArray(void);
    MetadataArray(const MetadataArray& otherMetadataArray) = delete;
    MetadataArray(MetadataArray&& otherMetadataArray) = delete;
    MetadataArray& operator=(const MetadataArray& rhsMetadataArray) = delete;
    MetadataArray& operator&(MetadataArray&& rhsMetadataArray) = delete;

    // interface functions
    bool init(const Array& phpArray = Array{});
    grpc_metadata* const data(void) { return m_Array.metadata; }
    const grpc_metadata* const data(void) const { return m_Array.metadata; }
    size_t size(void) const { return m_Array.count; }
    const grpc_metadata_array& array(void) const { return m_Array; } //
    grpc_metadata_array& array(void) { return m_Array; } // several methods need non const &
    Variant phpData(void) const;
    bool owned(void) const { return m_Owned; }

private:
    // helper functions
    void destroyPHP(void);
    void resizeMetadata(const size_t capacity);

    // member variables
    grpc_metadata_array m_Array;
    std::vector<std::pair<Slice, Slice>> m_PHPData; // the key, value PHP Data
    bool m_Owned;
};

/*****************************************************************************/
/*                               OpsManaged                                  */
/*****************************************************************************/

struct OpsManaged
{
public:
    // typedef's
    const static size_t s_MaxActions{ 8 };
    typedef std::array<grpc_byte_buffer*, s_MaxActions> MessagesType;

    // constructors/destructors
    OpsManaged(void);
    ~OpsManaged(void);
    OpsManaged(const OpsManaged& otherOpsManaged) = delete;
    OpsManaged(OpsManaged&& otherOpsManaged) = delete;
    OpsManaged& operator=(const OpsManaged& rhsOpsManaged) = delete;
    OpsManaged& operator&(OpsManaged&& rhsOpsManaged) = delete;

private:
    // helper fuctions
    void destroy(void);
    static void freeMessage(grpc_byte_buffer* pBuffer);

public:
    // member variabes
    MetadataArray send_metadata;                   // owned by caller
    MetadataArray send_trailing_metadata;          // owned by caller
    MetadataArray recv_metadata;                   // owned by call object
    MetadataArray recv_trailing_metadata;          // owned by call object
    MessagesType send_messages;                    // owned by caller
    MessagesType recv_messages;                    // owned by call object
    Slice recv_status_details;                     // owned by caller
    Slice send_status_details;                     // owned by caller
    int cancelled;
    grpc_status_code status;
};

/*****************************************************************************/
/*                             Channel Data                                  */
/*****************************************************************************/

// forward declarations
class ChannelData;
class CompletionQueue;

class CallData
{
public:
    // constructors/destructors
    CallData(void);
    CallData(grpc_call* const call, const bool owned, const int32_t timeoutMs = 0);
    ~CallData(void);
    CallData(const CallData& otherCallData) = delete;
    CallData(CallData&& otherCallData) = delete;
    CallData& operator=(const CallData& rhsCallData) = delete;
    CallData& operator&(CallData&& rhsCallData) = delete;
    void sweep(void);

    // interface functions
    void init(grpc_call* const call, const bool owned, const int32_t timeoutMs = 0);
    grpc_call* const call(void) { return m_pCall; }
    bool getOwned(void) const { return m_Owned; }
    bool credentialed(void) const { return (m_pCallCredentials != nullptr); }
    CallCredentialsData* const callCredentials(void) { return m_pCallCredentials; }
    void setCallCredentials(CallCredentialsData* const pCallCredentials) { m_pCallCredentials = pCallCredentials; }
    void setChannel(ChannelData* const pChannel) { m_pChannel = pChannel; }
    void setQueue(std::unique_ptr<CompletionQueue>&& pCompletionQueue);
    CompletionQueue* const queue(void) { return m_pCompletionQueue.get(); }
    int32_t getTimeout(void) const { return m_Timeout; }
    std::shared_ptr<MetadataPromise>& sharedPromise(void) { return m_pMetadataPromise; }
    std::shared_ptr<std::mutex>& sharedMutex(void) { return m_pMetadataMutex; }
    std::shared_ptr<bool>& sharedCancelled(void) { return m_pCallCancelled; }
    MetadataPromise& metadataPromise(void) { return *(m_pMetadataPromise.get()); }
    std::mutex& metadataMutex(void) { return *(m_pMetadataMutex.get()); }
    void setOpsManaged(std::unique_ptr<OpsManaged>&& pOpsManaged);
    OpsManaged& opsManaged(void) { return *(m_pOpsManaged.get()); }
    void incrementBatchCounter(void) { ++m_BatchCounter; }
    uint64_t batchCounter(void) const{ return m_BatchCounter; }
    void incrementActiveBatches(void) { ++m_ActiveBatches; }
    void decrementActiveBatches(void) { --m_ActiveBatches; }
    uint64_t activeBatches(void) const { return m_ActiveBatches; }
    bool& callCancelled(void) { return *(m_pCallCancelled.get()); }
    static Class* const getClass(void);
    static const StaticString& className(void) { return s_ClassName; }

private:
    // helper functions
    void destroy(void);

    // member variables
    grpc_call* m_pCall;
    bool m_Owned;
    CallCredentialsData* m_pCallCredentials;
    ChannelData* m_pChannel;
    int32_t m_Timeout;
    std::shared_ptr<MetadataPromise> m_pMetadataPromise; // metadata synchronization
    std::shared_ptr<std::mutex> m_pMetadataMutex;        // metadata synchronization
    std::shared_ptr<bool> m_pCallCancelled;              // metadata synchronizations
    std::unique_ptr<CompletionQueue> m_pCompletionQueue;
    std::unique_ptr<OpsManaged> m_pOpsManaged;
    std::atomic<uint64_t> m_BatchCounter;
    std::atomic<uint64_t> m_ActiveBatches;
    static Class* s_pClass;
    static const StaticString s_ClassName;
};

/*****************************************************************************/
/*                             HHVM Call Methods                             */
/*****************************************************************************/

void HHVM_METHOD(Call, __construct,
                 const Object& channel_obj,
                 const String& method,
                 const Object& deadline_obj,
                 const Variant& host_override /* = null */);

Object HHVM_METHOD(Call, startBatch,
                   const Array& actions);

String HHVM_METHOD(Call, getPeer);

void HHVM_METHOD(Call, cancel);

int64_t HHVM_METHOD(Call, setCredentials,
                    const Object& creds_obj);
};

#endif /* NET_GRPC_HHVM_GRPC_CALL_H_ */
