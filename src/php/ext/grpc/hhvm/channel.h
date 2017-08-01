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
#include <string>

namespace HPHP {

class ChannelData
{
public:
    // constructors/destructors
    ChannelData(void) : m_pChannel{ nullptr } {}
    ChannelData(grpc_channel* const channel) : m_pChannel{ channel } {}
    ~ChannelData(void);
    ChannelData(const ChannelData& otherChannelData) = delete;
    ChannelData(ChannelData&& otherChannelData) = delete;
    ChannelData& operator=(const ChannelData& rhsChannelData) = delete;
    ChannelData& operator=(ChannelData&& rhsChannelData) = delete;

    // interface functions
    void init(grpc_channel* channel) { m_pChannel = channel; }
    void sweep(void);
    grpc_channel* const getWrapped(void) { return m_pChannel; }
    void setHashKey(const String& hashKey) { m_HashKey = hashKey; }
    const String& getHashKey(void) const { return m_HashKey; }

    static Class* const getClass(void) { return s_Class; }
    static const StaticString& className(void) { return s_ClassName; }

 private:
     // member variables
    grpc_channel* m_pChannel;
    String m_HashKey;
    static Class* s_Class;
    static const StaticString s_ClassName;
};


class ChannelArgs
{
public:
    // constructors/destructors
    ChannelArgs(void);
    ~ChannelArgs(void);
    ChannelArgs(const ChannelData& otherChannelArgs) = delete;
    ChannelArgs(ChannelData&& otherChannelArgs) = delete;
    ChannelArgs& operator=(const ChannelData& rhsChannelArgs) = delete;
    ChannelArgs& operator=(ChannelData&& rhsChannelArgs) = delete;

    // interface functions
    bool init(const Array& argsArray);
    const grpc_channel_args& args(void) const { return m_ChannelArgs; }

private:
    // helper functions
    void destroyArgs(void);

    // member variables
    grpc_channel_args m_ChannelArgs;
};

/*

struct GlobalChannelsCache {
  std::forward_list<grpc_channel *> globalChannelMap;
};

extern GlobalChannelsCache s_global_channels_cache;
extern Mutex s_global_channels_cache_mutex;

struct ChannelsCache {
  ChannelsCache();
  void addChannel(const String& key, grpc_channel *channel);
  grpc_channel *getChannel(const String& key);
  bool hasChannel(const String& key);
  void deleteChannel(const String& key);

  std::map<std::string, grpc_channel *> channelMap;

  static DECLARE_THREAD_LOCAL(ChannelsCache, tl_obj);
};
*/


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
