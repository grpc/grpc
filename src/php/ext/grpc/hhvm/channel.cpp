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

#include <algorithm>
#include <vector>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "channel.h"
#include "common.h"
#include "completion_queue.h"
#include "channel_credentials.h"
#include "server.h"
#include "timeval.h"
#include "slice.h"

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/builtin-functions.h"
#include "hphp/runtime/base/string-util.h"
#include "hphp/runtime/vm/native-data.h"

namespace HPHP {

/*****************************************************************************/
/*                               Channel Data                                */
/*****************************************************************************/

Class* ChannelData::s_pClass{ nullptr };
const StaticString ChannelData::s_ClassName{ "Grpc\\Channel" };

Class* const ChannelData::getClass(void)
{
    if (!s_pClass)
    {
        s_pClass = Unit::lookupClass(s_ClassName.get());
        assert(s_pClass);
    }
    return s_pClass;
}

ChannelData::ChannelData(void) : m_pChannel{ nullptr }
{
}

ChannelData::ChannelData(grpc_channel* const channel) : m_pChannel{ channel }
{
}

ChannelData::~ChannelData(void)
{
    destroy();
}

void ChannelData::init(grpc_channel* channel, const bool owned, String&& hashKey)
{
    // destroy any existing channel data
    destroy();

    m_pChannel = channel;
    m_Owned = owned;
    m_HashKey = hashKey;
}

void ChannelData::destroy(void)
{
    if (m_pChannel)
    {
        if (m_Owned)
        {
            grpc_channel_destroy(m_pChannel);
        }
        m_pChannel = nullptr;
    }
}

/*****************************************************************************/
/*                            Channel Arguments                              */
/*****************************************************************************/

ChannelArgs::ChannelArgs(void)
{
    m_ChannelArgs.args = nullptr;
    m_ChannelArgs.num_args = 0;
}

ChannelArgs::~ChannelArgs(void)
{
    destroyArgs();
}

bool ChannelArgs::init(const Array& argsArray)
{
    // destroy existing
    destroyArgs();

    size_t elements{ static_cast<size_t>(argsArray.size()) };
    std::vector<std::pair<const char*, const char*>> channelArgs;
    if (elements > 0)
    {
        #if HHVM_VERSION_MAJOR >= 3 && HHVM_VERSION_MINOR >= 19 || HHVM_VERSION_MAJOR > 3
            m_ChannelArgs.args = (grpc_arg *) req::calloc_untyped(argsArray.size(), sizeof(grpc_arg));
        #else
            m_ChannelArgs.args = (grpc_arg *) req::calloc(argsArray.size(), sizeof(grpc_arg));
        #endif

        size_t count{ 0 };
        for (ArrayIter iter(argsArray); iter; ++iter, ++count)
        {
            Variant key{ iter.first() };
            if (key.isNull() || !key.isString())
            {
                destroyArgs();
                return false;
            }
            Slice keySlice{ key.toString() };

            Variant value{ iter.second() };
            if (!value.isNull())
            {
                if (value.isInteger())
                {
                    // convert and store PHP data
                    int32_t valueInt{ value.toInt32() };
                    Slice valueSlice{ std::to_string(valueInt) };
                    m_PHPData.emplace_back(std::move(keySlice), std::move(valueSlice));

                    m_ChannelArgs.args[count].value.integer = valueInt;
                    m_ChannelArgs.args[count].type = GRPC_ARG_INTEGER;
                }
                else if (value.isString())
                {
                    // convert and store PHP data
                    String valueStr{ value.toString() };
                    Slice valueSlice{ valueStr };
                    m_PHPData.emplace_back(std::move(keySlice), std::move(valueSlice));
                    m_ChannelArgs.args[count].value.string = reinterpret_cast<char*>(const_cast<uint8_t*>(m_PHPData[count].second.data()));
                    m_ChannelArgs.args[count].type = GRPC_ARG_STRING;
                }
                else
                {
                    destroyArgs();
                    return false;
                }
                m_ChannelArgs.args[count].key = reinterpret_cast<char*>(const_cast<uint8_t*>(m_PHPData[count].first.data()));
                channelArgs.emplace_back(reinterpret_cast<const char*>(m_PHPData[count].first.data()),
                                         reinterpret_cast<const char*>(m_PHPData[count].second.data()));
            }
            else
            {
                destroyArgs();
                return false;
            }

        }

        m_ChannelArgs.num_args = count;
    }

    // sort the channel arguments via key then value
    auto sortLambda = [](const std::pair<const char*, const char*>& pair1,
                         const std::pair<const char*, const char*>& pair2)
    {
        int keyCmp{ strcmp(pair1.first, pair2.first) };
        if (keyCmp != 0)
        {
            return keyCmp < 0;
        }
        else
        {
            return (strcmp(pair1.second, pair2.second) < 0);
        }
    };
    std::sort(channelArgs.begin(), channelArgs.end(), sortLambda);

    for(const auto& argPair : channelArgs)
    {
        m_ConcatenatedArgs += String{ argPair.first } + String{ argPair.second };
    }

    m_HashKey = StringUtil::SHA1(m_ConcatenatedArgs, false);

    return true;
}

void ChannelArgs::destroyArgs(void)
{
    // destroy channel args
    if (m_ChannelArgs.args)
    {
        req::free(m_ChannelArgs.args);
        m_ChannelArgs.args = nullptr;
    }
    m_ChannelArgs.num_args = 0;

    // destroy PHP data
    m_PHPData.clear();

    // reset cached values
    m_HashKey.clear();
    m_ConcatenatedArgs.clear();
}

/*****************************************************************************/
/*                               Channel Cache                               */
/*****************************************************************************/

ChannelsCache::ChannelsCache(void) : m_ChannelMapMutex{}, m_ChannelMap{}
{
}

ChannelsCache::~ChannelsCache(void)
{
    WriteLock lock{ m_ChannelMapMutex };
    for(auto& channelPair : m_ChannelMap)
    {
        destroyChannel(channelPair.second);
    }
    m_ChannelMap.clear();
}

ChannelsCache& ChannelsCache::getChannelsCache(void)
{
    static ChannelsCache s_ChannelsCache;
    return s_ChannelsCache;
}

std::pair<bool, grpc_channel* const>
ChannelsCache::addChannel(const String& key, grpc_channel* const pChannel)
{
    std::string keyString{ key.toCppString() };
    {
        WriteLock lock{ m_ChannelMapMutex };
        auto insertPair = m_ChannelMap.emplace(keyString, pChannel);
        if (!insertPair.second) return std::make_pair(false, insertPair.first->second);
        else                    return std::make_pair(true, insertPair.first->second);
    }
}

grpc_channel* const ChannelsCache::getChannel(const String& channelHash)
{
    ReadLock lock{ m_ChannelMapMutex };

    auto itrFind = m_ChannelMap.find(channelHash.toCppString());
    if (itrFind == m_ChannelMap.cend())
    {
        return nullptr;
    }
    else
    {
        return itrFind->second;
    }
}

bool ChannelsCache::hasChannel(const String& channelHash)
{
    return getChannel(channelHash) != nullptr;
}

void ChannelsCache::deleteChannel(const String& channelHash)
{
    std::string keyString{ channelHash.toCppString() };
    {
        WriteLock lock(m_ChannelMapMutex);

        auto itrFind = m_ChannelMap.find(keyString);
        if (itrFind != m_ChannelMap.cend())
        {
            destroyChannel(itrFind->second);
            m_ChannelMap.erase(itrFind);
        }
    }
}

size_t ChannelsCache::numChannels(void) const
{
    ReadLock lock{ m_ChannelMapMutex };
    return m_ChannelMap.size();
}

void ChannelsCache::destroyChannel(grpc_channel* const pChannel)
{
    grpc_channel_destroy(pChannel);
}

/*****************************************************************************/
/*                           HHVM Channel Methods                            */
/*****************************************************************************/

/**
 * Construct an instance of the Channel class.
 *
 * By default, the underlying grpc_channel is "persistent". That is, given
 * the same set of parameters passed to the constructor, the same underlying
 * grpc_channel will be returned.
 *
 * If the $args array contains a "credentials" key mapping to a
 * ChannelCredentials object, a secure channel will be created with those
 * credentials.
 *
 * If the $args array contains a "force_new" key mapping to a boolean value
 * of "true", a new underlying grpc_channel will be created regardless. If
 * there are any opened channels on the same hostname, user must manually
 * call close() on those dangling channels before the end of the PHP
 * script.
 *
 * @param string $target The hostname to associate with this channel
 * @param array $args_array The arguments to pass to the Channel
 */
void HHVM_METHOD(Channel, __construct,
                 const String& target,
                 const Array& args_array)
{
    HHVM_TRACE_SCOPE("Channel Construct") // Degug Trace

    ChannelData* const pChannelData{ Native::data<ChannelData>(this_) };

    ChannelCredentialsData* pChannelCredentialsData{ nullptr };
    String credentialsKey{ "credentials" };
    Array argsArrayCopy{ args_array.copy() };
    if (argsArrayCopy.exists(credentialsKey, true))
    {
        Variant value{ argsArrayCopy[credentialsKey] };
        if(!value.isNull() && value.isObject())
        {
            ObjectData* objData{ value.getObjectData() };
            if (!objData->instanceof(String("Grpc\\ChannelCredentials")))
            {
                SystemLib::throwInvalidArgumentExceptionObject("credentials must be a Grpc\\ChannelCredentials object");
            }
            Object obj{ value.toObject() };
            pChannelCredentialsData = Native::data<ChannelCredentialsData>(obj);
        }

        argsArrayCopy.remove(credentialsKey, true);
    }

    bool force_new{ false };
    String forceNewKey{ "force_new" };
    if (argsArrayCopy.exists(forceNewKey, true))
    {
        Variant value{ argsArrayCopy[forceNewKey] };
        if (!value.isNull() && value.isBoolean())
        {
            force_new = value.toBoolean();
        }

        argsArrayCopy.remove(forceNewKey, true);
    }

    ChannelArgs channelArgs{};
    if (!channelArgs.init(argsArrayCopy))
    {
        SystemLib::throwInvalidArgumentExceptionObject("invalid channel arguments");
    }

    String fullCacheKey{ StringUtil::SHA1(target + channelArgs.concatenatedArgs(), false) };
    if (pChannelCredentialsData != nullptr)
    {
        fullCacheKey += pChannelCredentialsData->hashKey();
    }
    grpc_channel* pChannel{ ChannelsCache::getChannelsCache().getChannel(fullCacheKey) };

    if (!force_new && pChannel)
    {
        pChannelData->init(pChannel, false, std::move(fullCacheKey));
    }
    else
    {
        if (force_new && pChannel)
        {
            // TODO: deleting an existing channel is problematic
            // the channel cache really needs to track all channels associated with
            // each hash along with a reference count on the channel and then if there are more
            // than 1 channels associated with the hash to delete the one with no references
            // during channel close
            // delete existing channel
            // ChannelsCache::getChannelsCache().deleteChannel(fullCacheKey);
        }

        if (!pChannelCredentialsData)
        {
            // no credentials create insecure channel
            pChannel = grpc_insecure_channel_create(target.c_str(), &channelArgs.args(), nullptr);
        }
        else
        {
            // create secure chhanel
            pChannel = grpc_secure_channel_create(pChannelCredentialsData->credentials(), target.c_str(),
                                                  &channelArgs.args(), nullptr);
        }

        if (!pChannel)
        {
            SystemLib::throwBadMethodCallExceptionObject("failed to create channel");
        }

        std::pair<bool, grpc_channel* const>
            addResult{ ChannelsCache::getChannelsCache().addChannel(fullCacheKey, pChannel) };
        if (!addResult.first)
        {
            // channel already cached.  This can happen if we currently force new and now
            // have two channels with same hash or via race conditions with multiple create channels
            // at same time
            //SystemLib::throwBadMethodCallExceptionObject("failed to create channel");

            // delete new channel and use existing
            grpc_channel_destroy(pChannel);
            pChannel = addResult.second;
        }
        pChannelData->init(pChannel, false, std::move(fullCacheKey));
    }
    //std::cout << ChannelsCache::getChannelsCache().numChannels() << std::endl;
}

/**
 * Get the endpoint this call/stream is connected to
 * @return string The URI of the endpoint
 */
String HHVM_METHOD(Channel, getTarget)
{
    HHVM_TRACE_SCOPE("Channel getTarget") // Degug Trace

    ChannelData* const pChannelData{ Native::data<ChannelData>(this_) };

    if (!pChannelData->channel())
    {
        SystemLib::throwBadMethodCallExceptionObject("Channel already closed.");
    }

    return String{ grpc_channel_get_target(pChannelData->channel()), CopyString };
}

/**
 * Get the connectivity state of the channel
 * @param bool $try_to_connect Try to connect on the channel (optional)
 * @return long The grpc connectivity state
 */
int64_t HHVM_METHOD(Channel, getConnectivityState,
                    bool try_to_connect /* = false */)
{
    ChannelData* const pChannelData{ Native::data<ChannelData>(this_) };

    if (!pChannelData->channel())
    {
        SystemLib::throwBadMethodCallExceptionObject("Channel already closed.");
    }

    grpc_connectivity_state state{ grpc_channel_check_connectivity_state(pChannelData->channel(),
                                                                         try_to_connect ? 1 : 0) };

    return static_cast<int64_t>(state);
}

/**
 * Watch the connectivity state of the channel until it changed
 * @param long $last_state The previous connectivity state of the channel
 * @param Timeval $deadline_obj The deadline this function should wait until
 * @return bool If the connectivity state changes from last_state
 *              before deadline
 */
bool HHVM_METHOD(Channel, watchConnectivityState,
                 int64_t last_state,
                 const Object& deadline)
{
    HHVM_TRACE_SCOPE("Channel watchConnectivityState") // Degug Trace

    ChannelData* const pChannelData{ Native::data<ChannelData>(this_) };

    if (!pChannelData->channel())
    {
        SystemLib::throwBadMethodCallExceptionObject("Channel already closed.");
    }

    TimevalData* const pTimevalDataDeadline{ Native::data<TimevalData>(deadline) };

    // TODO: In order to perform this we need to get the queue associated with the channel
    // which is associated with the call since each call gets own queue

   /* grpc_channel_watch_connectivity_state(pChannelData->channel(),
                                          static_cast<grpc_connectivity_state>(last_state),
                                          pTimevalDataDeadline->time(),
                                          CompletionQueue::getClientQueue().queue(),
                                          nullptr);

    grpc_event event( grpc_completion_queue_pluck(CompletionQueue::getClientQueue().queue(),
                                                  nullptr,
                                                  gpr_inf_future(GPR_CLOCK_REALTIME), nullptr) );

    return (event.success != 0);*/
    return true;
}

/**
 * Close the channel
 * @return void
 */
void HHVM_METHOD(Channel, close)
{
    HHVM_TRACE_SCOPE("Channel close") // Degug Trace

    ChannelData* const pChannelData{ Native::data<ChannelData>(this_) };

    if (!pChannelData->channel())
    {
        SystemLib::throwBadMethodCallExceptionObject("Channel already closed.");
    }
    pChannelData->init(nullptr, false, String{}); // mark channel closed
}

} // namespace HPHP
