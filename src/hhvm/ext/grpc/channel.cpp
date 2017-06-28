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

namespace HPHP {

Class* ChannelData::s_class = nullptr;
const StaticString ChannelData::s_className("Grpc\\Channel");

IMPLEMENT_GET_CLASS(ChannelData);

ChannelData::ChannelData() {}
ChannelData::~ChannelData() { sweep(); }

void ChannelData::init(grpc_channel* channel) {
  wrapped = channel;
}

void ChannelData::sweep() {
  if (wrapped) {
    grpc_channel_destroy(wrapped);
    wrapped = nullptr;
  }
}

grpc_channel* ChannelData::getWrapped() {
  return wrapped;
}

/**
 * Construct an instance of the Channel class. If the $args array contains a
 * "credentials" key mapping to a ChannelCredentials object, a secure channel
 * will be created with those credentials.
 * @param string $target The hostname to associate with this channel
 * @param array $args_array The arguments to pass to the Channel
 */
void HHVM_METHOD(Channel, __construct,
  const String& target,
  const Array& args_array) {
  auto argsArrayCopy = args_array.copy();

  auto channelData = Native::data<ChannelData>(this_);
  auto credentialsKey = String("credentials");

  ChannelCredentialsData* channelCredentialsData = NULL;

  if (argsArrayCopy.exists(credentialsKey, true)) {
    Variant value = argsArrayCopy[credentialsKey];
    if (value.isNull() || !value.isObject()) {
      argsArrayCopy.remove(credentialsKey, true);
    } else {
      Object obj = value.toObject();
      ObjectData* objData = value.getObjectData();
      if (!objData->instanceof(String("Grpc\\ChannelCredentials"))) {
        throw_invalid_argument("credentials must be a Grpc\\ChannelCredentials object");
        goto cleanup;
      }
      channelCredentialsData = Native::data<ChannelCredentialsData>(obj);
      argsArrayCopy.remove(credentialsKey, true);
    }
  }

  grpc_channel_args args;
  hhvm_grpc_read_args_array(argsArrayCopy, &args);

  if (channelCredentialsData == NULL) {
    channelData->init(grpc_insecure_channel_create(target.c_str(), &args, NULL));
  } else {
    channelData->init(grpc_secure_channel_create(channelCredentialsData->getWrapped(), target.c_str(), &args, NULL));
  }

  cleanup:
    req::free(args.args);
    return;
}

/**
 * Get the endpoint this call/stream is connected to
 * @return string The URI of the endpoint
 */
String HHVM_METHOD(Channel, getTarget) {
  auto channelData = Native::data<ChannelData>(this_);
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
  return (int64_t) grpc_channel_check_connectivity_state(channelData->getWrapped(),
                                                      (int)try_to_connect);
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
 delete channelData;
}


void hhvm_grpc_read_args_array(const Array& args_array, grpc_channel_args *args) {
  args->num_args = args_array.size();
  args->args = (grpc_arg *) req::calloc(args->num_args, sizeof(grpc_arg));

  int i = 0;
  for (ArrayIter iter(args_array); iter; ++iter) {
    Variant key = iter.first();
    if (!key.isString()) {
      throw_invalid_argument("args keys must be strings");
      return;
    }
    args->args[i].key = (char *)key.toString().c_str();

    Variant v = iter.second();
    
    if (key.isInteger()) {
      args->args[i].value.integer = v.toInt32();
      args->args[i].type = GRPC_ARG_INTEGER;
    } else if (key.isString()) {
      args->args[i].value.string = (char *)v.toString().c_str();
      args->args[i].type = GRPC_ARG_STRING;
    } else {
      throw_invalid_argument("args values must be int or string");
      return;
    }

    i++;
  }
}

} // namespace HPHP
