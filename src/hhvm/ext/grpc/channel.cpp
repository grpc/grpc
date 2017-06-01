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

#include "hhvm_grpc.h"

#include <stdbool.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include "completion_queue.h"
#include "channel_credentials.h"
#include "server.h"
#include "timeval.h"

const StaticString s_ChannelWrapper("ChannelWrapper");

class ChannelWrapper {
  private:
    grpc_channel* wrapped{nullptr};
  public:
    ChannelWrapper() {}
    ~ChannelWrapper() { sweep(); }

    void new(grpc_channel* channel) {
      memcpy(wrapped, channel, sizeof(grpc_channel));
    }

    void sweep() {
      if (wrapped) {
        grpc_channel_destroy(wrapped);
        req::free(wrapped);
        wrapped = nullptr;
      }
    }

    grpc_channel* getWrapped() {
      return wrapped;
    }
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
  auto channelWrapper = Native::data<ChannelWrapper>(this_);
  auto credentials_key = String("credentials");

  ChannelCredentialsWrapper* channel_credentials_wrapper = NULL;

  if (args_array.exists(credentials_key, true)) {
    Variant* value = args_array[credentials_key];
    if (value.isNull()) {
      args_array.remove(credentials_key, true);
    } else if (value.isObject()) {
      ObjectData* obj = value.getObjectData();
      if (!obj.instanceof(String("ChannelCredentials"))) {
        throw_invalid_argument("credentials must be a ChannelCredentials object");
        goto cleanup;
      }
    } else {
      channel_credentials_wrapper = Native::data<ChannelCredentialsWrapper>(obj);
      args_array.remove(credentials_key, true);
    }
  }

  grpc_channel_args args;
  hhvm_grpc_read_args_array(args_array, &args);

  if (channel_credentials_wrapper == NULL) {
    channelWrapper->new(grpc_insecure_channel_create(target.toCppString(), &args, NULL));
  } else {
    channelWrapper->new(grpc_secure_channel_create(channel_credentials_wrapper->getWrapped(), &args, NULL));
  }

  cleanup:
    req::free(args);
    return;
}

/**
 * Get the endpoint this call/stream is connected to
 * @return string The URI of the endpoint
 */
String HHVM_METHOD(Channel, getTarget) {
  auto channelWrapper = Native::data<ChannelWrapper>(this_);
  return String(grpc_channel_get_target(channelWrapper->getWrapped()), CopyString);
}

/**
 * Get the connectivity state of the channel
 * @param bool $try_to_connect Try to connect on the channel (optional)
 * @return long The grpc connectivity state
 */
int64_t HHVM_METHOD(Channel, getConnectivityState,
  bool try_to_connect /* = false */) {
  auto channelWrapper = Native::data<ChannelWrapper>(this_);
  return (int64_t) grpc_channel_check_connectivity_state(channelWrapper->getWrapped(),
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
  const Object& deadlineWrapper) {
  auto channelWrapper = Native::data<ChannelWrapper>(this_);

  grpc_channel_watch_connectivity_state(channelWrapper->getWrapped(),
                                          (grpc_connectivity_state)last_state,
                                          deadlineWrapper->getWrapped(), completion_queue,
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
 auto channelWrapper = Native::data<ChannelWrapper>(this_);
 delete channelWrapper;
}


void hhvm_grpc_read_args_array(const Array& args_array, grpc_channel_args *args) {
  args->num_args = args_array.size();
  args->args = req::calloc_untyped(args->num_args, sizeof(grpc_arg));

  for (ArrayIter iter(valuesArr); iter; ++iter) {
    int args_index = iter - 1;
    Variant key = iter.first();
    if (!key.isString()) {
      throw_invalid_argument("args keys must be strings");
      return;
    }
    args->args[args_index].key = key.toString().toCppString();

    Variant v = iter.second();
    
    if (key.isInteger()) {
      args->args[args_index].value.integer = v.toInt32();
      args->args[args_index].type = GRPC_ARG_INTEGER;
    } else if (key.isString()) {
      args->args[args_index].value.string = data.toString().toCppString();
      args->args[args_index].type = GRPC_ARG_STRING;
    } else {
      throw_invalid_argument("args values must be int or string");
      return;
    }
  }
}
