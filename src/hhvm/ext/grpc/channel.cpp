/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "channel.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

Channel::Channel() {}
Channel::~Channel() { sweep(); }

void Channel::init(grpc_channel* channel) {
  wrapped = channel;
}

void Channel::sweep() {
  if (wrapped) {
    grpc_channel_destroy(wrapped);
    wrapped = nullptr;
  }
}

grpc_channel* Channel::getWrapped() {
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
  auto args_array_ = args_array.copy();

  auto channel = Native::data<Channel>(this_);
  auto credentials_key = String("credentials");

  ChannelCredentials* channel_credentials_ = NULL;

  if (args_array_.exists(credentials_key, true)) {
    Variant value = args_array_[credentials_key];
    if (value.isNull() || !value.isObject()) {
      args_array_.remove(credentials_key, true);
    } else {
      ObjectData* obj = value.getObjectData();
      if (!obj->instanceof(String("ChannelCredentials"))) {
        throw_invalid_argument("credentials must be a ChannelCredentials object");
        goto cleanup;
      }
      channel_credentials_ = Native::data<ChannelCredentials>(obj);
      args_array_.remove(credentials_key, true);
    }
  }

  grpc_channel_args args;
  hhvm_grpc_read_args_array(args_array_, &args);

  if (channel_credentials_ == NULL) {
    channel->init(grpc_insecure_channel_create(target.c_str(), &args, NULL));
  } else {
    channel->init(grpc_secure_channel_create(channel_credentials_->getWrapped(), target.c_str(), &args, NULL));
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
  auto channel = Native::data<Channel>(this_);
  return String(grpc_channel_get_target(channel->getWrapped()), CopyString);
}

/**
 * Get the connectivity state of the channel
 * @param bool $try_to_connect Try to connect on the channel (optional)
 * @return long The grpc connectivity state
 */
int64_t HHVM_METHOD(Channel, getConnectivityState,
  bool try_to_connect /* = false */) {
  auto channel = Native::data<Channel>(this_);
  return (int64_t) grpc_channel_check_connectivity_state(channel->getWrapped(),
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
  auto channel = Native::data<Channel>(this_);

  auto timevalDeadline = Native::data<TimevalData>(deadline);

  grpc_channel_watch_connectivity_state(channel->getWrapped(),
                                          (grpc_connectivity_state)last_state,
                                          timevalDeadline->getWrapped(), completion_queue,
                                          NULL);

  grpc_event event = grpc_completion_queue_pluck(completion_queue, NULL,
                                  gpr_inf_future(GPR_CLOCK_REALTIME), NULL);

  return (bool)event.success;
}

/**
 * Close the channel
 * @return void
 */
void HHVM_METHOD(Channel, close) {
 auto channel = Native::data<Channel>(this_);
 delete channel;
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
