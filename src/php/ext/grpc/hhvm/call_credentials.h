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

typedef std::promise<bool> MetadataReturnPromise;

// this is the data passed back via promise from plugin_get_metadata
typedef struct plugin_get_metadata_params
{
  plugin_get_metadata_params(void* const _ptr, const std::string& _contextServiceUrl,
                             const std::string& _contextMethodName,
                             grpc_credentials_plugin_metadata_cb _cb,
                             void* const _user_data,
                             grpc_metadata* const _creds_md,
                             size_t* const _num_creds_md, grpc_status_code* const _status,
                             const char** const _error_details, const bool _completed = false,
                             const bool _result = false) :
        ptr{ _ptr },
        contextServiceUrl{ std::move(_contextServiceUrl) },
        contextMethodName{ std::move(_contextMethodName) }, cb{ _cb },
        user_data{ _user_data }, creds_md{ _creds_md },
        num_creds_md{ _num_creds_md }, status { _status },
        error_details { _error_details },
        completed{ _completed }, result{ _result } {}

  MetadataReturnPromise& returnPromise(void) { return m_ReturnPromise; }

  void *ptr;
  std::string contextServiceUrl;
  std::string contextMethodName;
  grpc_credentials_plugin_metadata_cb cb;
  void *user_data;
  grpc_metadata* creds_md;
  size_t *num_creds_md;
  grpc_status_code *status;
  const char ** error_details;
  bool completed;
  bool result;

  MetadataReturnPromise m_ReturnPromise;
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
        MetadataInfo(const std::thread::id& threadId = std::thread::id{ 0 }) :
            m_MetadataPromise{}, m_ThreadId{ threadId } {}
        ~MetadataInfo(void) = default;
        MetadataInfo(const MetadataInfo&) = delete;
        MetadataInfo(MetadataInfo&& otherMetadataInfo) :
            m_MetadataPromise{ std::move(otherMetadataInfo.m_MetadataPromise) },
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
        MetadataPromise& metadataPromise(void) { return m_MetadataPromise; }
        const std::thread::id& threadId(void) const { return m_ThreadId; }

    private:
        // member variables
        MetadataPromise m_MetadataPromise;
        std::thread::id m_ThreadId;

    private:
        // helper functions
        void swap(MetadataInfo& otherMetadataInfo)
        {
            // swap by move
            std::swap(m_MetadataPromise, otherMetadataInfo.m_MetadataPromise);
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
    void setInfo(CallCredentialsData* const pCallCredentials,
                 const std::shared_ptr<MetaDataInfo>& metaDataInfo);
    std::weak_ptr<MetaDataInfo> getInfo(CallCredentialsData* const pCallCredentials);
    bool deleteInfo(CallCredentialsData* const pCallCredentals);

    // singleton accessor
    static PluginMetadataInfo& getPluginMetadataInfo(void);
private:
    // constructors destructors
    PluginMetadataInfo(void) : m_MetaDataMap{} {}

    // member variables
    std::mutex m_Lock;
    std::unordered_map<CallCredentialsData*, std::weak_ptr<MetaDataInfo>> m_MetaDataMap;
};

bool plugin_do_get_metadata(void *ptr, const std::string& serviceURL,
                            const std::string& methodName,
                            grpc_credentials_plugin_metadata_cb cb, void* const user_data,
                            grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
                            size_t* const num_creds_md, grpc_status_code* const status,
                            const char** error_details);
}

#endif /* NET_GRPC_HHVM_GRPC_CALL_CREDENTIALS_H_ */
