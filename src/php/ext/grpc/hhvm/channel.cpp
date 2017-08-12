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

#include <map>
#include <string>

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

void ChannelData::init(grpc_channel* channel)
{
    // destroy any existing channel data
    destroy();

    m_pChannel = channel;
}

void ChannelData::destroy(void)
{
    if (m_pChannel)
    {
        // We don't destroy raw channels here because a channel may be shared by more than one object
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
    String objectKey = "";

    // destroy existing
    destroyArgs();

    size_t elements{ static_cast<size_t>(argsArray.size()) };
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
            m_ChannelArgs.args[count].key = const_cast<char*>(key.toString().c_str());

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
                    m_ChannelArgs.args[count].value.string = const_cast<char*>(value.toString().c_str());
                    m_ChannelArgs.args[count].type = GRPC_ARG_STRING;
                }
                else
                {
                    destroyArgs();
                    return false;
                }

                objectKey += key.toString() + value.toString();
            }
            else
            {
                destroyArgs();
                return false;
            }
        }

        m_ChannelArgs.num_args = count;
    }

    m_HashKey = StringUtil::SHA1(objectKey, false);

    return true;
}

void ChannelArgs::destroyArgs(void)
{
    if (m_ChannelArgs.args)
    {
        req::free(m_ChannelArgs.args);
        m_ChannelArgs.args = nullptr;
    }
    m_ChannelArgs.num_args = 0;
}

/*****************************************************************************/
/*                               Channel Cache                               */
/*****************************************************************************/

ChannelsCache::ChannelsCache(void) {}
ChannelsCache::~ChannelsCache(void) {
  WriteLock l(m_ChannelMapMutex);

  std::map<std::string, grpc_channel *>::iterator it;
  for (it = m_ChannelMap.begin(); it != m_ChannelMap.end(); ++it) {
    grpc_channel_destroy(it->second);
  }
  m_ChannelMap.clear();
}

void ChannelsCache::addChannel(const String& key, grpc_channel *channel) {
  WriteLock l(m_ChannelMapMutex);
  m_ChannelMap[key.toCppString()] = channel;
}

grpc_channel *ChannelsCache::getChannel(const String& key) {
  ReadLock l(m_ChannelMapMutex);
  return m_ChannelMap[key.toCppString()];
}

bool ChannelsCache::hasChannel(const String& key) {
  ReadLock l(m_ChannelMapMutex);
  std::map<std::string, grpc_channel *>::iterator it;
  it = m_ChannelMap.find(key.toCppString());
  if (it == m_ChannelMap.end()) {
    return false;
  }

  return true;
}

void ChannelsCache::deleteChannel(const String& key) {
  if (hasChannel(key)) {
    WriteLock l(m_ChannelMapMutex);
    m_ChannelMap.erase(key.toCppString());
  }
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

    String fullCacheKey = target + channelArgs.getHashKey();
    if (pChannelCredentialsData != nullptr)
    {
        fullCacheKey += pChannelCredentialsData->hashKey();
    }

    pChannelData->setHashKey(fullCacheKey);

    if (!force_new && ChannelsCache::GetChannelsCache().hasChannel(fullCacheKey))
    {
        pChannelData->init(ChannelsCache::GetChannelsCache().getChannel(fullCacheKey));
    }
    else
    {
      grpc_channel* pChannel{ nullptr };
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
      pChannelData->init(pChannel);

      ChannelsCache::GetChannelsCache().addChannel(fullCacheKey, pChannel);
    }
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

    grpc_channel_watch_connectivity_state(pChannelData->channel(),
                                          static_cast<grpc_connectivity_state>(last_state),
                                          pTimevalDataDeadline->time(),
                                          CompletionQueue::getClientQueue().queue(),
                                          nullptr);

    grpc_event event( grpc_completion_queue_pluck(CompletionQueue::getClientQueue().queue(),
                                                  nullptr,
                                                  gpr_inf_future(GPR_CLOCK_REALTIME), nullptr) );

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
    }

    ChannelsCache::GetChannelsCache().deleteChannel(pChannelData->getHashKey());

    // destruct the channel
    //grpc_channel_destroy(pChannelData->channel()); // todo: can we destroy here? Channel's are shared and may be in use by other objects
    pChannelData->init(nullptr);
}

} // namespace HPHP
