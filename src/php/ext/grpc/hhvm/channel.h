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

#include <map>
#include <string>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "hphp/runtime/ext/extension.h"

#include <grpc/grpc.h>

namespace HPHP {

/*****************************************************************************/
/*                               Channel Data                                */
/*****************************************************************************/

class ChannelData
{
public:
    // constructors/destructors
    ChannelData(void);
    ChannelData(grpc_channel* const channel);
    ~ChannelData(void);
    ChannelData(const ChannelData& otherChannelData) = delete;
    ChannelData(ChannelData&& otherChannelData) = delete;
    ChannelData& operator=(const ChannelData& rhsChannelData) = delete;
    ChannelData& operator=(ChannelData&& rhsChannelData) = delete;

    // interface functions
    void init(grpc_channel* channel);
    grpc_channel* const channel(void) { return m_pChannel; }
    void setHashKey(const String& hashKey) { m_HashKey = hashKey; }
    const String& getHashKey(void) const { return m_HashKey; }
    static Class* const getClass(void);
    static const StaticString& className(void) { return s_ClassName; }

 private:
    // helper functions
    void destroy(void);

    // member variables
    grpc_channel* m_pChannel;
    String m_HashKey;
    static Class* s_pClass;
    static const StaticString s_ClassName;
};

/*****************************************************************************/
/*                             Channel Arguments                             */
/*****************************************************************************/

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
    const String& getHashKey(void) { return m_HashKey; }

private:
    // helper functions
    void destroyArgs(void);

    // member variables
    String m_HashKey;
    grpc_channel_args m_ChannelArgs;
};

/*****************************************************************************/
/*                               Channel Cache                               */
/*****************************************************************************/

class ChannelsCache
{
public:
  static ChannelsCache& GetChannelsCache(void)
  {
      static ChannelsCache s_ChannelsCache;
      return s_ChannelsCache;
  }

  // constructors/destructors
  ChannelsCache(void);
  ~ChannelsCache(void);

  // interface functions
  void addChannel(const String& key, grpc_channel *channel);
  grpc_channel *getChannel(const String& key);
  bool hasChannel(const String& key);
  void deleteChannel(const String& key);
  void destroyChannels(void);
private:
  // member variables
  ReadWriteMutex m_ChannelMapMutex;
  std::map<std::string, grpc_channel *> m_ChannelMap;
};

/*****************************************************************************/
/*                              HHVM Channel Methods                         */
/*****************************************************************************/

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

}

#endif /* NET_GRPC_HHVM_GRPC_CHANNEL_H_ */
