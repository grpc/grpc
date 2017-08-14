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

#ifndef NET_GRPC_HHVM_GRPC_SERVER_CREDENTIALS_H_
#define NET_GRPC_HHVM_GRPC_SERVER_CREDENTIALS_H_

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "common.h"

#include "hphp/runtime/ext/extension.h"

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

namespace HPHP {

/*****************************************************************************/
/*                            Server Credentials Data                        */
/*****************************************************************************/

class ServerCredentialsData
{
public:
    // constructors/destructors
    ServerCredentialsData(void);
    ~ServerCredentialsData(void);
    ServerCredentialsData(const ServerCredentialsData& otherServerCredentialsData) = delete;
    ServerCredentialsData(ServerCredentialsData&& otherServerCredentialsData) = delete;
    ServerCredentialsData& operator=(const ServerCredentialsData& rhsServerCredentialsData) = delete;
    ServerCredentialsData& operator=(ServerCredentialsData&& rhsServerCredentialsData) = delete;

    // interface functions
    void init(grpc_server_credentials* const server_credentials);
    grpc_server_credentials* const credentials(void) { return m_pCredentials; }
    static Class* const getClass(void);
    static const StaticString& className(void) { return s_ClassName; }

private:
    // helper functions
    void destroy(void);

    // member variables
    grpc_server_credentials* m_pCredentials;
    static Class* s_pClass;
    static const StaticString s_ClassName;
};

/*****************************************************************************/
/*                       HHVM Server Credentials Methods                     */
/*****************************************************************************/

Object HHVM_STATIC_METHOD(ServerCredentials, createSsl,
                          const String& pem_root_certs,
                          const String& pem_private_key,
                          const String& pem_cert_chain);

}

#endif /* NET_GRPC_HHVM_GRPC_SERVER_CREDENTIALS_H_ */
