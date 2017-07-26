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

#include "channel.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "common.h"

#include <stdbool.h>
#include <map>
#include <string>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include "completion_queue.h"
#include "channel_credentials.h"
#include "server.h"
#include "timeval.h"

#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/req-containers.h"
#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/builtin-functions.h"
#include "hphp/runtime/ext/std/ext_std_variable.h"
#include "hphp/runtime/base/variable-unserializer.h"
#include "hphp/runtime/base/string-util.h"

namespace HPHP {

GlobalChannelsCache s_global_channels_cache;
Mutex s_global_channels_cache_mutex;

Class* ChannelData::s_class = nullptr;
const StaticString ChannelData::s_className("Grpc\\Channel");

IMPLEMENT_GET_CLASS(ChannelData);

ChannelData::ChannelData() {}
ChannelData::~ChannelData() { sweep(); }

void ChannelData::init(grpc_channel* channel) {
  wrapped = channel;
}

void ChannelData::sweep() {}

grpc_channel* ChannelData::getWrapped() {
  return wrapped;
}

void ChannelData::setHashKey(const String& hashKey) {
  key = hashKey;
}

String ChannelData::getHashKey() {
  return key;
}

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
  const Array& args_array) {
  bool force_new = false;
  auto argsArrayCopy = args_array.copy();

  auto channelData = Native::data<ChannelData>(this_);
  auto credentialsKey = String("credentials");
  auto forceNewKey = String("force_new");

  ChannelCredentialsData* channelCredentialsData = NULL;

  if (argsArrayCopy.exists(credentialsKey, true)) {
    Variant value = argsArrayCopy[credentialsKey];
    if (value.isObject() && !value.isNull()) {
      Object obj = value.toObject();
      ObjectData* objData = value.getObjectData();
      if (!objData->instanceof(String("Grpc\\ChannelCredentials"))) {
        throw_invalid_argument("credentials must be a Grpc\\ChannelCredentials object");
        return;
      }
      channelCredentialsData = Native::data<ChannelCredentialsData>(obj);
    }

    argsArrayCopy.remove(credentialsKey, true);
  }

  if (argsArrayCopy.exists(forceNewKey, true)) {
    Variant value = argsArrayCopy[forceNewKey];
    if (value.isBoolean() && !value.isNull()) {
      force_new = value.toBoolean();
    }

    argsArrayCopy.remove(forceNewKey, true);
  }

  String serializedArgsArray = HHVM_FN(serialize)(argsArrayCopy);
  String serializedHash = StringUtil::SHA1(serializedArgsArray, false);
  String hashKey = target + serializedHash;

  if (channelCredentialsData != NULL) {
    hashKey += channelCredentialsData->getHashKey();
  }

  channelData->setHashKey(hashKey);

  if (force_new) {
    ChannelsCache::tl_obj.get()->deleteChannel(hashKey);
  }

  if (ChannelsCache::tl_obj.get()->hasChannel(hashKey)) {
    channelData->init(ChannelsCache::tl_obj.get()->getChannel(hashKey));
  } else {
    grpc_channel_args args;
    if (hhvm_grpc_read_args_array(argsArrayCopy, &args) == -1) {
      req::free(args.args);
      return;
    }

    grpc_channel *channel;
    if (channelCredentialsData == NULL) {
      channel = grpc_insecure_channel_create(target.c_str(), &args, NULL);
    } else {
      channel = grpc_secure_channel_create(channelCredentialsData->getWrapped(), target.c_str(), &args, NULL);
    }

    channelData->init(channel);
    ChannelsCache::tl_obj.get()->addChannel(hashKey, channel);

    req::free(args.args);
  }
}

/**
 * Get the endpoint this call/stream is connected to
 * @return string The URI of the endpoint
 */
String HHVM_METHOD(Channel, getTarget) {
  auto channelData = Native::data<ChannelData>(this_);
  if (channelData->getWrapped() == nullptr) {
    SystemLib::throwInvalidArgumentExceptionObject("Channel already closed.");
  }

  return String(grpc_channel_get_target(channelData->getWrapped()), CopyString);
}

/**
 * Get the connectivity state of the channel
 * @param bool $try_to_connect Try to connect on the channel (optional)
 * @return long The grpc connectivity state
 */
int64_t HHVM_METHOD(Channel, getConnectivityState,
  bool try_to_connect /* = false */) {
  auto channelData = Native::data<ChannelData>(this_);
  if (channelData->getWrapped() == nullptr) {
    SystemLib::throwInvalidArgumentExceptionObject("Channel already closed.");
  }

  int state = grpc_channel_check_connectivity_state(channelData->getWrapped(),
                                                      (int)try_to_connect);

  // this can happen if another shared Channel object close the underlying
  // channel
  if (state == GRPC_CHANNEL_SHUTDOWN) {
    channelData->init(nullptr);
  }

  return (int64_t) state;
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
  const Object& deadline) {
  auto channelData = Native::data<ChannelData>(this_);
  if (channelData->getWrapped() == nullptr) {
    SystemLib::throwInvalidArgumentExceptionObject("Channel already closed.");
  }

  auto timevalDataDeadline = Native::data<TimevalData>(deadline);

  grpc_channel_watch_connectivity_state(channelData->getWrapped(),
                                          (grpc_connectivity_state)last_state,
                                          timevalDataDeadline->getWrapped(), CompletionQueue::tl_obj.get()->getQueue(),
                                          NULL);

  grpc_event event = grpc_completion_queue_pluck(CompletionQueue::tl_obj.get()->getQueue(), NULL,
                                  gpr_inf_future(GPR_CLOCK_REALTIME), NULL);

  return (bool)event.success;
}

/**
 * Close the channel
 * @return void
 */
void HHVM_METHOD(Channel, close) {
 auto channelData = Native::data<ChannelData>(this_);
 if (channelData->getWrapped() == nullptr) {
   SystemLib::throwInvalidArgumentExceptionObject("Channel already closed.");
 }

 ChannelsCache::tl_obj.get()->deleteChannel(channelData->getHashKey());

 delete channelData;
}


int hhvm_grpc_read_args_array(const Array& args_array, grpc_channel_args *args) {
  args->num_args = args_array.size();
  args->args = (grpc_arg *) req::calloc(args->num_args, sizeof(grpc_arg));

  int i = 0;
  for (ArrayIter iter(args_array); iter; ++iter) {
    Variant key = iter.first();
    if (!key.isString()) {
      throw_invalid_argument("args keys must be strings");
      return -1;
    }
    args->args[i].key = (char *)key.toString().c_str();

    Variant v = iter.second();
    
    if (v.isInteger()) {
      args->args[i].value.integer = v.toInt32();
      args->args[i].type = GRPC_ARG_INTEGER;
    } else if (v.isString()) {
      args->args[i].value.string = (char *)v.toString().c_str();
      args->args[i].type = GRPC_ARG_STRING;
    } else {
      throw_invalid_argument("args values must be int or string");
      return -1;
    }

    i++;
  }

  return 0;
}

} // namespace HPHP
