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

#ifndef NET_GRPC_HHVM_GRPC_CALL_CREDENTIALS_H_
#define NET_GRPC_HHVM_GRPC_CALL_CREDENTIALS_H_

#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_map>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "hphp/runtime/ext/extension.h"

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

namespace HPHP {

/*****************************************************************************/
/*                         Call Credentials Data                             */
/*****************************************************************************/

class CallCredentialsData
{
public:
    // constructors/destructors
    CallCredentialsData(void);
    ~CallCredentialsData(void);
    CallCredentialsData(const CallCredentialsData& otherCallCredentialsData) = delete;
    CallCredentialsData(CallCredentialsData&& otherCallCredentialsData) = delete;
    CallCredentialsData& operator=(const CallCredentialsData& rhsCallCredentialsData) = delete;
    CallCredentialsData& operator&(CallCredentialsData&& rhsCallCredentialsData) = delete;
    void sweep(void);

    // interface functions
    void init(grpc_call_credentials* const pCallCredentials);
    grpc_call_credentials* const credentials(void)  { return m_pCallCredentials; }
    static Class* const getClass(void);
    static const StaticString& className(void) { return s_ClassName; }

private:
    // helper functions
    void destroy(void);

    // member variables
    grpc_call_credentials* m_pCallCredentials;
    static Class* s_pClass;
    static const StaticString s_ClassName;
};

/*****************************************************************************/
/*                      HHVM Call Credentials Methods                        */
/*****************************************************************************/
Object HHVM_STATIC_METHOD(CallCredentials, createComposite,
                          const Object& cred1_obj,
                          const Object& cred2_obj);

Object HHVM_STATIC_METHOD(CallCredentials, createFromPlugin,
                          const Variant& callback);

/*****************************************************************************/
/*                       Crendentials Plugin Functions                       */
/*****************************************************************************/

// this is the data passed back via promise from plugin_get_metadata
typedef struct plugin_get_metadata_params
{
    plugin_get_metadata_params(void* const _ptr, std::string&& _contextServiceUrl,
                               std::string&& _contextMethodName,
                               grpc_credentials_plugin_metadata_cb&& _cb,
                               void* const _user_data, const bool _completed = false) :
        completed{ _completed }, ptr{ _ptr },
        contextServiceUrl{ std::move(_contextServiceUrl) },
        contextMethodName{ std::move(_contextMethodName) },
        cb{ std::move(_cb) }, user_data{ _user_data } {}
    bool completed;
    void *ptr;
    std::string contextServiceUrl;
    std::string contextMethodName;
    grpc_credentials_plugin_metadata_cb cb;
    void *user_data;
} plugin_get_metadata_params;

typedef std::promise<plugin_get_metadata_params> MetadataPromise;

// this is a singleton class which manages the map of call credentials to promise and other data.
//  The promise is actually held by the calls
class PluginMetadataInfo
{
public:
    // typedefs
    typedef class MetadataInfo
    {
    public:
        // constructors/destructors
        MetadataInfo(const std::shared_ptr<MetadataPromise>& pMetadataPromise = std::shared_ptr<MetadataPromise>{ nullptr },
                     const std::shared_ptr<std::mutex>& pMetadataMutex = std::shared_ptr<std::mutex>{ nullptr },
                     const std::shared_ptr<bool>& pCallCancelled = std::shared_ptr<bool>{ nullptr },
                     const std::thread::id& threadId = std::thread::id{ 0 }) :
            m_pMetadataPromise{ pMetadataPromise }, m_pMetadataMutex{ pMetadataMutex }, m_pCallCancelled{ pCallCancelled },
            m_ThreadId{ threadId } {}
        ~MetadataInfo(void) = default;
        MetadataInfo(const MetadataInfo&) = delete;
        MetadataInfo(MetadataInfo&& otherMetadataInfo) :
            m_pMetadataPromise{ std::move(otherMetadataInfo.m_pMetadataPromise) },
            m_pMetadataMutex{ std::move(otherMetadataInfo.m_pMetadataMutex) },
            m_pCallCancelled{ std::move(otherMetadataInfo.m_pCallCancelled) },
            m_ThreadId{ std::move(otherMetadataInfo.m_ThreadId) } {};
        MetadataInfo& operator=(const MetadataInfo&) = delete;
        MetadataInfo& operator=(MetadataInfo&& rhsMetadataInfo)
        {
            if (this != &rhsMetadataInfo)
            {
                MetadataInfo tempMetadataInfo{ std::move(rhsMetadataInfo) };
                swap(tempMetadataInfo);
            }
            return *this;
        }

        // interface functions
        MetadataPromise* const metadataPromise(void) { return m_pMetadataPromise.get(); }
        std::mutex* const metadataMutex(void) { return m_pMetadataMutex.get(); }
        const bool* const callCancelled(void) const { return m_pCallCancelled.get(); }
        const std::thread::id& threadId(void) const { return m_ThreadId; }

    private:
        // member variables
        std::shared_ptr<MetadataPromise> m_pMetadataPromise;
        std::shared_ptr<std::mutex> m_pMetadataMutex;
        std::shared_ptr<bool> m_pCallCancelled;
        std::thread::id m_ThreadId;

    private:
        // helper functions
        void swap(MetadataInfo& otherMetadataInfo)
        {
            // swap by move
            std::swap(m_pMetadataPromise, otherMetadataInfo.m_pMetadataPromise);
            std::swap(m_pMetadataMutex, otherMetadataInfo.m_pMetadataMutex);
            std::swap(m_pCallCancelled, otherMetadataInfo.m_pCallCancelled);
            std::swap(m_ThreadId, otherMetadataInfo.m_ThreadId);
        }

    } MetaDataInfo;

    // constructors/destructors
    ~PluginMetadataInfo(void);
    PluginMetadataInfo(const PluginMetadataInfo& otherPluginMetadataInfo) = delete;
    PluginMetadataInfo(PluginMetadataInfo&& otherPluginMetadataInfo) = delete;
    PluginMetadataInfo& operator=(const PluginMetadataInfo& rhsPluginMetadataInfo) = delete;
    PluginMetadataInfo& operator&(PluginMetadataInfo&& rhsPluginMetadataInfo) = delete;

    // interface functions
    void setInfo(CallCredentialsData* const pCallCredentials, MetaDataInfo&& metaDataInfo);
    MetaDataInfo getInfo(CallCredentialsData* const pCallCredentials);
    bool deleteInfo(CallCredentialsData* const pCallCredentals);

    // singleton accessor
    static PluginMetadataInfo& getPluginMetadataInfo(void);
private:
    // constructors destructors
    PluginMetadataInfo(void) : m_MetaDataMap{} {}

    // member variables
    std::mutex m_Lock;
    std::unordered_map<CallCredentialsData*, MetaDataInfo> m_MetaDataMap;
};

void plugin_do_get_metadata(void *ptr, const std::string& serviceURL,
                            const std::string& methodName,
                            grpc_credentials_plugin_metadata_cb cb,
                            void *user_data);
}

#endif /* NET_GRPC_HHVM_GRPC_CALL_CREDENTIALS_H_ */
