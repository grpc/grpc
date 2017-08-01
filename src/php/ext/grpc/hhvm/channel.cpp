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

#include <stdbool.h>
#include <map>
#include <string>

#include "channel.h"
#include "completion_queue.h"
#include "channel_credentials.h"
#include "server.h"
#include "timeval.h"
#include "utility.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/req-containers.h"
#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/builtin-functions.h"
#include "hphp/runtime/ext/std/ext_std_variable.h"
#include "hphp/runtime/base/variable-unserializer.h"
#include "hphp/runtime/base/string-util.h"

namespace HPHP {

/*
GlobalChannelsCache s_global_channels_cache;
Mutex s_global_channels_cache_mutex;
*/

/*****************************************************************************/
/*                               ChannelData                                 */
/*****************************************************************************/

Class* ChannelData::s_Class{ nullptr };
const StaticString ChannelData::s_ClassName{ "Grpc\\Channel" };

ChannelData::~ChannelData(void)
{
    destroy();
}

void ChannelData::destroy(void)
{
    if (m_pChannel)
    {
        grpc_channel_destroy(m_pChannel);
        m_pChannel = nullptr;
    }
}

/*****************************************************************************/
/*                               ChannelArgs                                 */
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
    if (elements > 0 )
    {
        #if HHVM_VERSION_MAJOR >= 3 && HHVM_VERSION_MINOR >= 19
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
            m_ChannelArgs.args[count].key = const_cast<char*>(key.getStringData()->data());

            Variant value{ iter.second() };
            if (!value.isNull())
            {
                if (value.isInteger())
                {
                    m_ChannelArgs.args[count].value.integer = value.toInt32();
                    m_ChannelArgs.args[count].type = GRPC_ARG_INTEGER;
                }
                else if (value.isString())
                {
                    m_ChannelArgs.args[count].value.string = const_cast<char*>(value.getStringData()->data());
                    m_ChannelArgs.args[count].type = GRPC_ARG_STRING;
                    std::cout << count << ' ' << m_ChannelArgs.args[count].key << ' '
                              << m_ChannelArgs.args[count].value.string << std::endl;
                }
                else
                {
                    destroyArgs();
                    return false;
                }
            }
            else
            {
                destroyArgs();
                return false;
            }

        }

        m_ChannelArgs.num_args = count;
        return true;
    }
    return true;
}

void ChannelArgs::destroyArgs(void)
{
    if (m_ChannelArgs.args)
    {
        req::free(m_ChannelArgs.args);
        m_ChannelArgs.args = nullptr;
    }
    m_ChannelArgs.num_args=0;
}


/*
IMPLEMENT_THREAD_LOCAL(ChannelsCache, ChannelsCache::tl_obj);

ChannelsCache::ChannelsCache() {}
void ChannelsCache::addChannel(const String& key, grpc_channel *channel) {
  channelMap[key.toCppString()] = channel;

  {
    Lock l1(s_global_channels_cache_mutex);
    s_global_channels_cache.globalChannelMap.push_front(channel);
  }
}

grpc_channel *ChannelsCache::getChannel(const String& key) {
  return channelMap[key.toCppString()];
}

bool ChannelsCache::hasChannel(const String& key) {
  std::map<std::string, grpc_channel *>::iterator it;
  it = channelMap.find(key.toCppString());
  if (it == channelMap.end()) {
    return false;
  }

  return true;
}

void ChannelsCache::deleteChannel(const String& key) {
  if (hasChannel(key)) {
    channelMap.erase(key.toCppString());
  }
}
*/

/*****************************************************************************/
/*                               HHVM Methods                                */
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
                return;
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

    /*
    String serializedArgsArray = HHVM_FN(serialize)(argsArrayCopy);
    String serializedHash = StringUtil::SHA1(serializedArgsArray, false);
    String hashKey = target + serializedHash;

    if (channelCredentialsData != nullptr)
    {
        hashKey += channelCredentialsData->getHashKey();
    }

    pChannelData->setHashKey(hashKey);

    if (force_new)
    {
        ChannelsCache::tl_obj.get()->deleteChannel(hashKey);
    }

    if (ChannelsCache::tl_obj.get()->hasChannel(hashKey))
    {
        pChannelData->init(ChannelsCache::tl_obj.get()->getChannel(hashKey));
    }
    else {
    */

    ChannelArgs channelArgs{};
    if (!channelArgs.init(argsArrayCopy))
    {
        SystemLib::throwInvalidArgumentExceptionObject("invalid channel arguments");
        return;
    }

    grpc_channel* pChannel{ nullptr };
    std::cout << "Target " << target.c_str() << std::endl;
    if (!pChannelCredentialsData)
    {
        // no credentials create insecure channel
        pChannel = grpc_insecure_channel_create(target.c_str(), &channelArgs.args(), nullptr);
    }
    else
    {
        // create secure chhanel
        pChannel = grpc_secure_channel_create(pChannelCredentialsData->getWrapped(), target.c_str(),
                                              &channelArgs.args(), nullptr);
    }

    if (!pChannel)
    {
        SystemLib::throwBadMethodCallExceptionObject("failed to create channel");
        return;
    }
    pChannelData->init(pChannel);

    //ChannelsCache::tl_obj.get()->addChannel(hashKey, channel);
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
        return String{};
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
        return GRPC_CHANNEL_SHUTDOWN;
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
        return GRPC_CHANNEL_SHUTDOWN;
    }

    TimevalData* const pTimevalDataDeadline{ Native::data<TimevalData>(deadline) };

    grpc_channel_watch_connectivity_state(pChannelData->channel(),
                                          static_cast<grpc_connectivity_state>(last_state),
                                          pTimevalDataDeadline->getWrapped(),
                                          CompletionQueue::getQueue().queue(),
                                          nullptr);

    grpc_event event{ grpc_completion_queue_pluck(CompletionQueue::getQueue().queue(), nullptr,
                      gpr_inf_future(GPR_CLOCK_REALTIME), nullptr) };

    return (event.success != 0);
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
        return;
    }

     //ChannelsCache::tl_obj.get()->deleteChannel(channelData->getHashKey());

     // destruct the channel
     pChannelData->~ChannelData();
}

} // namespace HPHP
