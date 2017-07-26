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

#ifndef NET_GRPC_HHVM_GRPC_CHANNEL_H_
#define NET_GRPC_HHVM_GRPC_CHANNEL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hphp/runtime/ext/extension.h"

#include <grpc/grpc.h>

#include <map>

namespace HPHP {

class ChannelData {
  private:
    grpc_channel* wrapped{nullptr};
    String key;
  public:
    static Class* s_class;
    static const StaticString s_className;

    static Class* getClass();

    ChannelData();
    ~ChannelData();

    void init(grpc_channel* channel);
    void sweep();
    grpc_channel* getWrapped();
    void setHashKey(const String& hashKey);
    String getHashKey();
};

struct ChannelsCache {
  ChannelsCache();
  void addChannel(const String& key, grpc_channel *channel);
  grpc_channel *getChannel(const String& key);
  bool hasChannel(const String& key);
  void deleteChannel(const String& key);

  std::map<String, grpc_channel *> channelMap;

  static DECLARE_THREAD_LOCAL(ChannelsCache, tl_obj);
};

void HHVM_METHOD(Channel, __construct,
  const String& target,
  const Array& args_array);

String HHVM_METHOD(Channel, getTarget);

int64_t HHVM_METHOD(Channel, getConnectivityState,
  bool try_to_connect /* = false */);

bool HHVM_METHOD(Channel, watchConnectivityState,
  int64_t last_state,
  const Object& deadline);

void HHVM_METHOD(Channel, close);

int hhvm_grpc_read_args_array(const Array& args_array, grpc_channel_args *args);

}

#endif /* NET_GRPC_HHVM_GRPC_CHANNEL_H_ */
