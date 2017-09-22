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

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "server_credentials.h"
#include "common.h"

#include "hphp/runtime/vm/native-data.h"

namespace HPHP {

/*****************************************************************************/
/*                             Server Credentials Data                       */
/*****************************************************************************/

Class* ServerCredentialsData::s_pClass{ nullptr };
const StaticString ServerCredentialsData::s_ClassName{ "Grpc\\ServerCredentials" };

Class* const ServerCredentialsData::getClass(void)
{
    if (!s_pClass)
    {
        s_pClass = Unit::lookupClass(s_ClassName.get());
        assert(s_pClass);
    }
    return s_pClass;
}

ServerCredentialsData::ServerCredentialsData(void) : m_pCredentials{ nullptr }
{
}

ServerCredentialsData::~ServerCredentialsData()
{
    destroy();
}

void ServerCredentialsData::sweep(void)
{
    destroy();
}

void ServerCredentialsData::init(grpc_server_credentials* const server_credentials)
{
    // destroy any existing server credentials
    destroy();

    m_pCredentials = server_credentials;
}

void ServerCredentialsData::destroy(void)
{
    if (m_pCredentials)
    {
        grpc_server_credentials_release(m_pCredentials);
        m_pCredentials = nullptr;
    }
}

/*****************************************************************************/
/*                         HHVM Server Credentials Methods                   */
/*****************************************************************************/

Object HHVM_STATIC_METHOD(ServerCredentials, createSsl,
                          const String& pem_root_certs,
                          const String& pem_private_key,
                          const String& pem_cert_chain)
{
    HHVM_TRACE_SCOPE("ServerCredentials createSsl") // Debug Trace

    std::string pemRootCerts{ pem_root_certs.toCppString() };

    grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
    std::string privateKey{ pem_private_key.toCppString() };
    pem_key_cert_pair.private_key = privateKey.c_str();
    std::string certChain{ pem_cert_chain.toCppString() };
    pem_key_cert_pair.cert_chain = certChain.c_str();

    Object newServerCredentialsObj{ ServerCredentialsData::getClass() };
    ServerCredentialsData* const pServerCredentialsData{ Native::data<ServerCredentialsData>(newServerCredentialsObj) };

    // TODO: add a client_certificate_request field in ServerCredentials and pass it as the last parameter. */
    grpc_server_credentials* const pServerCredentials{
        grpc_ssl_server_credentials_create_ex(pemRootCerts.c_str(), &pem_key_cert_pair,
                                              1, GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE, nullptr) };

    if (!pServerCredentials)
    {
        SystemLib::throwBadMethodCallExceptionObject("failed to create server credentials");
    }

    pServerCredentialsData->init(pServerCredentials);

    return newServerCredentialsObj;
}

} // namespace HPHP
