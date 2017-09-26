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

#include "channel_credentials.h"
#include "call_credentials.h"
#include "common.h"

#include "hphp/runtime/vm/native-data.h"
#include "hphp/runtime/base/string-util.h"

#include "grpc/support/alloc.h"

namespace HPHP {

/*****************************************************************************/
/*                     Default Permanent Root Certificates                   */
/*****************************************************************************/

DefaultPEMRootCerts::DefaultPEMRootCerts(void) : m_PEMRootCerts{}
{
}

grpc_ssl_roots_override_result DefaultPEMRootCerts::get_ssl_roots_override(char** pPermRootsCerts)
{
    // The output of this function (the char *) is gpr_free'd by the calling function
    DefaultPEMRootCerts& singletonCerts{ getDefaultPEMRootCerts() };

    {
        ReadLock lock{ singletonCerts.m_CertsLock };

        if (singletonCerts.m_PEMRootCerts.empty())
        {
            return GRPC_SSL_ROOTS_OVERRIDE_FAIL;
        }

        // allocate and copy string with null
        size_t strLength{ singletonCerts.m_PEMRootCerts.size() + 1};
        char* const pStr{ reinterpret_cast<char *>(gpr_zalloc(strLength)) };
        if (pStr)
        {
            std::memcpy(reinterpret_cast<void*>(pStr),
                        reinterpret_cast<const void*>(singletonCerts.m_PEMRootCerts.c_str()), strLength);
            *pPermRootsCerts = pStr;
            return GRPC_SSL_ROOTS_OVERRIDE_OK;
        }
        else
        {

            return GRPC_SSL_ROOTS_OVERRIDE_FAIL;
        }
    }
}

void DefaultPEMRootCerts::setCerts(std::string&& pemRootsCerts)
{
    {
        WriteLock lock{ m_CertsLock };

        // copy new certs
        m_PEMRootCerts = std::move(pemRootsCerts);
    }
}

DefaultPEMRootCerts& DefaultPEMRootCerts::getDefaultPEMRootCerts(void)
{
    static DefaultPEMRootCerts s_DefaultPEMRootCerts{};
    return s_DefaultPEMRootCerts;
}

/*****************************************************************************/
/*                        Channel Credentials Data                           */
/*****************************************************************************/

Class* ChannelCredentialsData::s_pClass{ nullptr };
const StaticString ChannelCredentialsData::s_ClassName{ "Grpc\\ChannelCredentials" };

Class* const ChannelCredentialsData::getClass(void)
{
    if (!s_pClass)
    {
        s_pClass = Unit::lookupClass(s_ClassName.get());
        assert(s_pClass);
    }
    return s_pClass;
}

ChannelCredentialsData::ChannelCredentialsData(void) :
    m_pChannelCredentials{ nullptr }, m_HashKey{ String{} }
{
}

ChannelCredentialsData::~ChannelCredentialsData(void)
{
    destroy();
}

void ChannelCredentialsData::sweep(void)
{
    destroy();
}

void ChannelCredentialsData::init(grpc_channel_credentials* const pChannelCredentials,
                                  const std::string& hashKey)
{
    // forward
    init(pChannelCredentials, std::move(std::string{ hashKey }));
}

void ChannelCredentialsData::init(grpc_channel_credentials* const pChannelCredentials,
                                  std::string&& hashKey)
{
    // destroy any existing channel credentials
    destroy();

    m_pChannelCredentials = pChannelCredentials;
    m_HashKey = std::move(hashKey);
}

void ChannelCredentialsData::destroy(void)
{
    if (m_pChannelCredentials)
    {
        grpc_channel_credentials_release(m_pChannelCredentials);
        m_pChannelCredentials = nullptr;
    }
}

/*****************************************************************************/
/*                     HHVM Channel Credentials Methods                      */
/*****************************************************************************/

void HHVM_STATIC_METHOD(ChannelCredentials, setDefaultRootsPem,
                        const String& pem_root_certs)
{
    HHVM_TRACE_SCOPE("ChannelCredentials setDefaultRootsPem") // Debug Trace

    std::string pemRootCerts{ pem_root_certs.toCppString() };
    DefaultPEMRootCerts::getDefaultPEMRootCerts().setCerts(std::move(pemRootCerts));
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createDefault)
{
    HHVM_TRACE_SCOPE("ChannelCredentials createDefault") // Debug Trace

    Object newChannelCredentialsObj{ ChannelCredentialsData::getClass() };
    ChannelCredentialsData* const pChannelCredentialsData{ Native::data<ChannelCredentialsData>(newChannelCredentialsObj) };
    grpc_channel_credentials* const pChannelCredentials{ grpc_google_default_credentials_create() };

    if (!pChannelCredentials)
    {
        SystemLib::throwBadMethodCallExceptionObject("Failed to create default channel credentials");
    }

    pChannelCredentialsData->init(pChannelCredentials, std::string{});

    return newChannelCredentialsObj;
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createSsl,
                          const Variant& pem_root_certs /*=null*/,
                          const Variant& pem_key_cert_pair__private_key /*= null*/,
                          const Variant& pem_key_cert_pair__cert_chain /*=null*/
                          )
{
    HHVM_TRACE_SCOPE("ChannelCredentials createSsl") // Debug Trace

    std::string pemRootCerts{ (!pem_root_certs.isNull() && pem_root_certs.isString()) ?
                              pem_root_certs.toString().toCppString() : "" };

    Object newChannelCredentialsObj{ ChannelCredentialsData::getClass() };
    ChannelCredentialsData* const pChannelCredentialsData{ Native::data<ChannelCredentialsData>(newChannelCredentialsObj) };

    std::string unhashedKey{};
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
    pem_key_cert_pair.private_key = pem_key_cert_pair.cert_chain = nullptr;

    std::string privateKey{};
    if (!pem_key_cert_pair__private_key.isNull() && pem_key_cert_pair__private_key.isString())
    {
        privateKey = std::move(pem_key_cert_pair__private_key.toString().toCppString());
        pem_key_cert_pair.private_key = privateKey.c_str();
        unhashedKey += privateKey;
    }

    std::string certChain{};
    if (!pem_key_cert_pair__cert_chain.isNull() && pem_key_cert_pair__cert_chain.isString())
    {
        certChain = std::move(pem_key_cert_pair__cert_chain.toString().toCppString());
        pem_key_cert_pair.cert_chain = certChain.c_str();
        unhashedKey += certChain;
    }

    std::string hashKey{ !unhashedKey.empty() ?
                         StringUtil::SHA1(unhashedKey, false).toCppString() : "" };

    grpc_channel_credentials* const pChannelCredentials{
        grpc_ssl_credentials_create(!pemRootCerts.empty() ? pemRootCerts.c_str() : nullptr,
                                    !pem_key_cert_pair.private_key ? nullptr : &pem_key_cert_pair,
                                    nullptr) };

    if (!pChannelCredentials)
    {
        SystemLib::throwBadMethodCallExceptionObject("Failed to create SSL channel credentials");
    }

    pChannelCredentialsData->init(pChannelCredentials, std::move(hashKey));

    return newChannelCredentialsObj;
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createComposite,
                          const Object& cred1_obj,
                          const Object& cred2_obj)
{
    HHVM_TRACE_SCOPE("ChannelCredentials createComposite") // Debug Trace

    ChannelCredentialsData* pChannelCredentialsData{ Native::data<ChannelCredentialsData>(cred1_obj) };
    CallCredentialsData* pCallCredentialsData{ Native::data<CallCredentialsData>(cred2_obj) };

    grpc_channel_credentials* const pChannelCredentials{
        grpc_composite_channel_credentials_create(pChannelCredentialsData->credentials(),
                                                  pCallCredentialsData->credentials(), nullptr) };

    if (!pChannelCredentials)
    {
        SystemLib::throwBadMethodCallExceptionObject("Failed to create composite channel credentials");
    }

    Object newChannelCredentialsObj{ChannelCredentialsData::getClass() };
    ChannelCredentialsData* const pNewChannelCredentialsData{ Native::data<ChannelCredentialsData>(newChannelCredentialsObj) };
    pNewChannelCredentialsData->init(pChannelCredentials, pChannelCredentialsData->hashKey());

    return newChannelCredentialsObj;
}

Variant HHVM_STATIC_METHOD(ChannelCredentials, createInsecure)
{
    HHVM_TRACE_SCOPE("ChannelCredentials createInsecure") // Debug Trace

    return Variant{};
}

void grpc_hhvm_init_channel_credentials(void)
{
    static std::mutex s_CallLock;

    {
        // NOTE:  This function call is not thread safe and requires an exclusive lock
        std::unique_lock<std::mutex> lock{ s_CallLock };
        grpc_set_ssl_roots_override_callback(DefaultPEMRootCerts::get_ssl_roots_override);
    }
}

} // namespace HPHP
