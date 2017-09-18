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

#ifndef NET_GRPC_HHVM_GRPC_SERVER_H_
#define NET_GRPC_HHVM_GRPC_SERVER_H_

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "completion_queue.h"

#include "hphp/runtime/ext/extension.h"

#include "grpc/grpc.h"

namespace HPHP {

/*****************************************************************************/
/*                                  Server Data                              */
/*****************************************************************************/

class ServerData
{
public:
    // constructors/destructors
    ServerData(void);
    ~ServerData(void);
    ServerData(const ServerData& otherServerData) = delete;
    ServerData(ServerData&& otherServerData) = delete;
    ServerData& operator=(const ServerData& rhsServerData) = delete;
    ServerData& operator=(ServerData&& rhsServerData) = delete;
    void sweep(void);

    // interface functions
    void init(grpc_server* const pServer);
    grpc_server* const server(void) { return m_pServer; }
    CompletionQueue* const queue(void) { return m_pComletionQueue.get(); };
    static Class* const getClass(void);
    static const StaticString& className(void) { return s_ClassName; }

private:
    // helper functions
    void destroy(void);

    // member variables
    grpc_server* m_pServer;
    std::unique_ptr<CompletionQueue> m_pComletionQueue;
    static Class* s_pClass;
    static const StaticString s_ClassName;
};

/*****************************************************************************/
/*                           HHVM Server Methods                             */
/*****************************************************************************/

void HHVM_METHOD(Server, __construct,
                 const Variant& args_array_or_null /* = null */);

Object HHVM_METHOD(Server, requestCall);

bool HHVM_METHOD(Server, addHttp2Port,
                 const String& addr);

bool HHVM_METHOD(Server, addSecureHttp2Port,
                 const String& addr,
                 const Object& server_credentials);

void HHVM_METHOD(Server, start);

}

#endif /* NET_GRPC_HHVM_GRPC_SERVER_H_ */
