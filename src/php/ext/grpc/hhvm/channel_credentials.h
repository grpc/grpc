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

#ifndef NET_GRPC_HHVM_GRPC_CHANNEL_CREDENTIALS_H_
#define NET_GRPC_HHVM_GRPC_CHANNEL_CREDENTIALS_H_

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "common.h"
#include "slice.h"

#include "hphp/runtime/ext/extension.h"

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

namespace HPHP {

/*****************************************************************************/
/*                     Default Permanent Root Certificates                   */
/*****************************************************************************/

// This is a global singleton. It does not make sense to make this thread local
// as differnent threads may access the default certs and they must be valid
// in all.
struct DefaultPermRootCerts
{
public:
    // constructors/destructors
    DefaultPermRootCerts(const DefaultPermRootCerts& otherDefaultPermRootCerts) = delete;
    DefaultPermRootCerts(DefaultPermRootCerts&& otherDefaultPermRootCerts) = delete;
    DefaultPermRootCerts& operator=(const DefaultPermRootCerts& rhsDefaultPermRootCerts) = delete;
    DefaultPermRootCerts& operator=(DefaultPermRootCerts&& rhsDefaultPermRootCerts) = delete;

    // interface functions
    Slice getCerts(void);
    void setCerts(const String& permRootsCerts);
    static grpc_ssl_roots_override_result get_ssl_roots_override(char** pPermRootsCerts);

    // singleton accessors
    static DefaultPermRootCerts& getDefaultPermRootCerts(void);

private:
    // private constructor
    DefaultPermRootCerts(void);

    // member variables
    Slice m_PermRootCerts;
};

/*****************************************************************************/
/*                           Channel Credentials Data                        */
/*****************************************************************************/

class ChannelCredentialsData
{
public:
    // constructors/destructors
    ChannelCredentialsData(void);
    ~ChannelCredentialsData(void);
    ChannelCredentialsData(const ChannelCredentialsData& otherChannelCredentialsData) = delete;
    ChannelCredentialsData(ChannelCredentialsData&& otherChannelCredentialsData) = delete;
    ChannelCredentialsData& operator=(const ChannelCredentialsData& rhsChannelCredentialsData) = delete;
    ChannelCredentialsData& operator&(ChannelCredentialsData&& rhsChannelCredentialsData) = delete;

    // interface functions
    void init(grpc_channel_credentials* const pChannelCredentials, const String& hashKey = String{});
    grpc_channel_credentials* const credentials(void) { return m_pChannelCredentials; }
    const String& hashKey(void) const { return m_HashKey; }
    static Class* const getClass(void);
    static const StaticString& className(void) { return s_ClassName; }

private:
    // helper functions
    void destroy(void);

    // member variables
    grpc_channel_credentials* m_pChannelCredentials;
    String m_HashKey;
    static Class* s_pClass;
    static const StaticString s_ClassName;

};

/*****************************************************************************/
/*                       HHVM Channel Credentials Methods                    */
/*****************************************************************************/

void HHVM_STATIC_METHOD(ChannelCredentials, setDefaultRootsPem,
                        const String& perm_root_certs);

Object HHVM_STATIC_METHOD(ChannelCredentials, createDefault);

Object HHVM_STATIC_METHOD(ChannelCredentials, createSsl,
                          const Variant& perm_root_certs,
                          const Variant& perm_key_cert_pair__private_key /*= null*/,
                          const Variant& perm_key_cert_pair__cert_chain /*=null*/
                         );

Object HHVM_STATIC_METHOD(ChannelCredentials, createComposite,
                          const Object& cred1_obj,
                          const Object& cred2_obj);

Variant HHVM_STATIC_METHOD(ChannelCredentials, createInsecure);

void grpc_hhvm_init_channel_credentials(void);

}

#endif /* NET_GRPC_HHVM_GRPC_CHANNEL_CREDENTIALS_H_ */
