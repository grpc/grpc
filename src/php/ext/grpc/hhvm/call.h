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

#ifndef NET_GRPC_HHVM_GRPC_CALL_H_
#define NET_GRPC_HHVM_GRPC_CALL_H_

#include <cstdint>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "hphp/runtime/ext/extension.h"

#include "grpc/grpc.h"

namespace HPHP {

// forward declarations
class ChannelData;

class CallData
{
public:
    // constructors/destructors
    CallData(void) : m_pCall{ nullptr }, m_Owned{ false },
        m_ChannelData{ nullptr }, m_Timeout{ 0 } {}
    CallData(grpc_call* const call) : m_pCall{ call },
        m_Owned{ false }, m_ChannelData{ nullptr }, m_Timeout{ 0 } {}
    ~CallData(void);
    CallData(const CallData& otherCallData) = delete;
    CallData(CallData&& otherCallData) = delete;
    CallData& operator=(const CallData& otherCallData) = delete;
    CallData& operator&(CallData&& otherCallData) = delete;

    // interface functions
    void sweep(void);
    void init(grpc_call* call) { m_pCall = call; }
    grpc_call* const getWrapped(void) { return m_pCall; }
    bool getOwned(void) const { return m_Owned; }
    void setChannelData(ChannelData* channelData) { m_ChannelData = channelData; }
    void setOwned(const bool owned) { m_Owned = owned; }
    void setTimeout(const int32_t timeout) { m_Timeout = timeout; }
    int32_t getTimeout(void) const { return m_Timeout; }

    static Class* const getClass(void) { return s_Class; }
    static const StaticString& className(void) { return s_ClassName; }

private:
    // member variables
    grpc_call* m_pCall;
    bool m_Owned;
    ChannelData* m_ChannelData;
    int32_t m_Timeout;
    static Class* s_Class;
    static const StaticString s_ClassName;
};

void HHVM_METHOD(Call, __construct,
                 const Object& channel_obj,
                 const String& method,
                 const Object& deadline_obj,
                 const Variant& host_override /* = null */);

Object HHVM_METHOD(Call, startBatch,
                   const Array& actions);

String HHVM_METHOD(Call, getPeer);

void HHVM_METHOD(Call, cancel);

int64_t HHVM_METHOD(Call, setCredentials,
                    const Object& creds_obj);
};

#endif /* NET_GRPC_HHVM_GRPC_CALL_H_ */
