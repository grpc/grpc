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

#include "call_credentials.h"
#include "call.h"
#include "channel_credentials.h"
#include "common.h"

#include "hphp/runtime/base/array-init.h"
#include "hphp/runtime/base/builtin-functions.h"
#include "hphp/runtime/vm/native-data.h"

#include "grpc/support/alloc.h"

namespace HPHP {

/*****************************************************************************/
/*                       Crendentials Plugin Functions                       */
/*****************************************************************************/

typedef struct plugin_state
{
    Variant callback;
    CallCredentialsData* pCallCredentials;
} plugin_state;

// forward declarations
void plugin_get_metadata(void *ptr, grpc_auth_metadata_context context,
                         grpc_credentials_plugin_metadata_cb cb,
                         void *user_data);
void plugin_destroy_state(void *ptr);

PluginMetadataInfo::~PluginMetadataInfo(void)
{
    std::lock_guard<std::mutex> lock{ m_Lock };
    m_MetaDataMap.clear();
}

PluginMetadataInfo& PluginMetadataInfo::getPluginMetadataInfo(void)
{
    static PluginMetadataInfo s_PluginMetadataInfo;
    return s_PluginMetadataInfo;
}

void PluginMetadataInfo::setInfo(CallCredentialsData* const pCallCredentials,
                                 MetaDataInfo&& metaDataInfo)
{
    std::lock_guard<std::mutex> lock{ m_Lock };
    auto itrPair = m_MetaDataMap.emplace(pCallCredentials, std::move(metaDataInfo));
    if (!itrPair.second)
    {
        // call credentials exist already so update
        // TODO:  The second entry may need to be a vector if we have multiple
        // stacked but current thinking is this can't happen
        itrPair.first->second = std::move(metaDataInfo);
    }
}

typename PluginMetadataInfo::MetaDataInfo
PluginMetadataInfo::getInfo(CallCredentialsData* const pCallCredentials)
{
    MetaDataInfo metaDataInfo{};
    {
        std::lock_guard<std::mutex> lock{ m_Lock };
        auto itrFind = m_MetaDataMap.find(pCallCredentials);
        if (itrFind != m_MetaDataMap.cend())
        {
            // get the metadata info
            metaDataInfo = std::move(itrFind->second);

            // erase the entry
            m_MetaDataMap.erase(itrFind);
        }
    }
    return metaDataInfo;
}

bool PluginMetadataInfo::deleteInfo(CallCredentialsData* const pCallCredentials)
{
    std::lock_guard<std::mutex> lock{ m_Lock };
    auto itrFind = m_MetaDataMap.find(pCallCredentials);
    if (itrFind != m_MetaDataMap.cend())
    {
        // erase the entry
        m_MetaDataMap.erase(itrFind);
        return true;
    }
    else
    {
        // does not exist
        return false;
    }
}

/*****************************************************************************/
/*                           Call Credentials Data                           */
/*****************************************************************************/

Class* CallCredentialsData::s_pClass{ nullptr };
const StaticString CallCredentialsData::s_ClassName{ "Grpc\\CallCredentials" };

Class* const CallCredentialsData::getClass(void)
{
    if (!s_pClass)
    {
        s_pClass = Unit::lookupClass(s_ClassName.get());
        assert(s_pClass);
    }
    return s_pClass;
}

CallCredentialsData::CallCredentialsData(void) : m_pCallCredentials{ nullptr }
{
}

CallCredentialsData::~CallCredentialsData(void)
{
    destroy();
}

void CallCredentialsData::init(grpc_call_credentials* const pCallCredentials)
{
    // destroy any existing call credetials
    destroy();

    // take ownership of new call credentials
    m_pCallCredentials = pCallCredentials;
}

void CallCredentialsData::destroy(void)
{
    if (m_pCallCredentials)
    {
        grpc_call_credentials_release(m_pCallCredentials);
        m_pCallCredentials = nullptr;
    }
}

/*****************************************************************************/
/*                        HHVM Call Credentials Methods                      */
/*****************************************************************************/

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

    CallCredentialsData* const pCallCredentialsData1{ Native::data<CallCredentialsData>(cred1_obj) };
    CallCredentialsData* const pCallCredentialsData2{ Native::data<CallCredentialsData>(cred2_obj) };

    grpc_call_credentials* const pCallCredentials{
        grpc_composite_call_credentials_create(pCallCredentialsData1->credentials(),
                                               pCallCredentialsData2->credentials(),
                                               nullptr) };

    if (!pCallCredentials)
    {
        SystemLib::throwBadMethodCallExceptionObject("Failed to create call credentials composite");
    }

    Object newCallCredentialsObj{ CallCredentialsData::getClass() };
    CallCredentialsData* const pNewCallCredentialsData{ Native::data<CallCredentialsData>(newCallCredentialsObj)};
    pNewCallCredentialsData->init(pCallCredentials);

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

    if (callback.isNull() || !is_callable(callback))
    {
        SystemLib::throwInvalidArgumentExceptionObject("Callback argument is not a valid callback");
    }

    Object newCallCredentialsObj{ CallCredentialsData::getClass() };
    CallCredentialsData* const pNewCallCredentialsData{ Native::data<CallCredentialsData>(newCallCredentialsObj) };

    plugin_state *pState{ reinterpret_cast<plugin_state*>(gpr_zalloc(sizeof(plugin_state))) };
    pState->callback = callback;
    pState->pCallCredentials = pNewCallCredentialsData;

    grpc_metadata_credentials_plugin plugin;
    plugin.get_metadata = plugin_get_metadata;
    plugin.destroy = plugin_destroy_state;
    plugin.state = reinterpret_cast<void *>(pState);
    plugin.type = "";

    grpc_call_credentials* pCallCredentials{ grpc_metadata_credentials_create_from_plugin(plugin, nullptr) };

    if (!pCallCredentials)
    {
        SystemLib::throwBadMethodCallExceptionObject("failed to create call credntials plugin");
    }
    pNewCallCredentialsData->init(pCallCredentials);

    return newCallCredentialsObj;
}

/*****************************************************************************/
/*                       Crendentials Plugin Functions                       */
/*****************************************************************************/

// This work done in this function MUST be done on the same thread as the HHVM call request
void plugin_do_get_metadata(void *ptr, grpc_auth_metadata_context context,
                            grpc_credentials_plugin_metadata_cb cb,
                            void *user_data)
{
    HHVM_TRACE_SCOPE("CallCredentials plugin_do_get_metadata") // Degug Trace

    Object returnObj{ SystemLib::AllocStdClassObject() };
    returnObj.o_set("service_url", String(context.service_url, CopyString));
    returnObj.o_set("method_name", String(context.method_name, CopyString));

    plugin_state* const pState{ reinterpret_cast<plugin_state *>(ptr) };

    Variant retVal{ vm_call_user_func(pState->callback, make_packed_array(returnObj)) };
    if (!retVal.isArray())
    {
        SystemLib::throwInvalidArgumentExceptionObject("Callback return value expected an array.");
    }

    grpc_status_code code{ GRPC_STATUS_OK };
    MetadataArray metadata;
    if (!metadata.init(retVal.toArray()))
    {
        code = GRPC_STATUS_INVALID_ARGUMENT;
    }

    // Pass control back to core
    cb(user_data, metadata.data(), metadata.size(), code, nullptr);
}

void plugin_get_metadata(void *ptr, grpc_auth_metadata_context context,
                         grpc_credentials_plugin_metadata_cb cb,
                         void *user_data)
{
    HHVM_TRACE_SCOPE("CallCredentials plugin_get_metadata") // Degug Trace

    plugin_state *pState{ reinterpret_cast<plugin_state *>(ptr) };
    CallCredentialsData* const pCallCrendentials{ pState->pCallCredentials };

    PluginMetadataInfo& pluginMetaDataInfo{ PluginMetadataInfo::getPluginMetadataInfo() };
    PluginMetadataInfo::MetaDataInfo metaDataInfo{ pluginMetaDataInfo.getInfo(pCallCrendentials) };

    MetadataPromise* const pMetaDataPromise{ metaDataInfo.metadataPromise() };
    if (!pMetaDataPromise)
    {
        // failed to get promise associated with call credentials.  This can happen if the call timed
        // out and the metadata was erased before this function was invoked
        return;
    }

    std::mutex& metaDataMutex{ *(metaDataInfo.metadataMutex()) };
    const bool& callCancelled{ *(metaDataInfo.callCancelled()) };
    const std::thread::id& callThreadId{ metaDataInfo.threadId() };
    if (callThreadId == std::this_thread::get_id())
    {
        HHVM_TRACE_SCOPE("CallCredentials plugin_get_metadata same thread") // Degug Trace
        plugin_get_metadata_params params{ ptr, std::move(context), std::move(cb), user_data,
                                           true };
        {
            // check if call cancelled from timeout before performing callback function
            std::lock_guard<std::mutex> lock{ metaDataMutex };
            if (!callCancelled)
            {
                plugin_do_get_metadata(ptr, context, cb, user_data);
            }
            else
            {
                // call was cancelled
                return;
            }
        }
        pMetaDataPromise->set_value(std::move(params));
    }
    else
    {
        HHVM_TRACE_SCOPE("CallCredentials plugin_get_metadata different thread") // Degug Trace
        plugin_get_metadata_params params{ ptr, std::move(context), std::move(cb), user_data };

        // return the meta data params in the promise
        pMetaDataPromise->set_value(std::move(params));
    }
}

void plugin_destroy_state(void *ptr)
{
    HHVM_TRACE_SCOPE("CallCredentials plugin_destroy_state") // Degug Trace

    plugin_state* const pState{ reinterpret_cast<plugin_state *>(ptr) };
    if (pState) gpr_free(pState);
}

} // namespace HPHP
