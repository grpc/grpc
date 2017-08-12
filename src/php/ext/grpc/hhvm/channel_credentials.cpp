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

#include <grpc/support/alloc.h>

namespace HPHP {

IMPLEMENT_THREAD_LOCAL(DefaultPemRootCerts, DefaultPemRootCerts::tl_obj);

static grpc_ssl_roots_override_result get_ssl_roots_override(char **pem_root_certs) {
  *pem_root_certs = DefaultPemRootCerts::tl_obj.get()->getCerts();
  if (*pem_root_certs == nullptr) {
    return GRPC_SSL_ROOTS_OVERRIDE_FAIL;
  }
  return GRPC_SSL_ROOTS_OVERRIDE_OK;
}

DefaultPemRootCerts::DefaultPemRootCerts() {}

char * DefaultPemRootCerts::getCerts() { return default_pem_root_certs; }
void DefaultPemRootCerts::setCerts(const String& pem_roots) {
  if (default_pem_root_certs != nullptr) {
    gpr_free((void *)default_pem_root_certs);
  }

  default_pem_root_certs = (char *) gpr_malloc((pem_roots.length() + 1) * sizeof(char));
  memcpy(default_pem_root_certs, pem_roots.c_str(), pem_roots.length() + 1);
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

void ChannelCredentialsData::init(grpc_channel_credentials* const channel_credentials)
{
    // destroy any existing channel credentials
    destroy();

    m_pChannelCredentials = channel_credentials;
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
                        const String& pem_roots)
{
    HHVM_TRACE_SCOPE("ChannelCredentials setDefaultRootsPem") // Degug Trace

  DefaultPemRootCerts::tl_obj.get()->setCerts(pem_roots);
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createDefault)
{
    HHVM_TRACE_SCOPE("ChannelCredentials createDefault") // Degug Trace

  auto newChannelCredentialsObj = Object{ChannelCredentialsData::getClass()};
  auto channelCredentialsData = Native::data<ChannelCredentialsData>(newChannelCredentialsObj);
  grpc_channel_credentials *channel_credentials = grpc_google_default_credentials_create();
  channelCredentialsData->init(channel_credentials);

  return newChannelCredentialsObj;
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createSsl,
                          const Variant& pem_root_certs /*=null*/,
                          const Variant& pem_key_cert_pair__private_key /*= null*/,
                          const Variant& pem_key_cert_pair__cert_chain /*=null*/
                          )
{
    HHVM_TRACE_SCOPE("ChannelCredentials createSsl") // Degug Trace

  const char *pem_root_certs_ = nullptr;

  if (pem_root_certs.isString()) {
    pem_root_certs_ = pem_root_certs.toString().c_str();
  }

  auto newChannelCredentialsObj = Object{ChannelCredentialsData::getClass()};
  auto channelCredentialsData = Native::data<ChannelCredentialsData>(newChannelCredentialsObj);

  String unhashedKey = String("");

  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
  pem_key_cert_pair.private_key = pem_key_cert_pair.cert_chain = nullptr;

  if (pem_key_cert_pair__private_key.isString()) {
    pem_key_cert_pair.private_key = pem_key_cert_pair__private_key.toString().c_str();
    unhashedKey += pem_key_cert_pair__private_key.toString();
  }

  if (pem_key_cert_pair__cert_chain.isString()) {
    pem_key_cert_pair.cert_chain = pem_key_cert_pair__cert_chain.toString().c_str();
    unhashedKey += pem_key_cert_pair__cert_chain.toString();
  }

  if (unhashedKey != String("")) {
    String hashKey = StringUtil::SHA1(unhashedKey, false);
    channelCredentialsData->setHashKey(hashKey);
  } else {
    channelCredentialsData->setHashKey(String(""));
  }

  channelCredentialsData->init(grpc_ssl_credentials_create(
    pem_root_certs_,
    pem_key_cert_pair.private_key == nullptr ? nullptr : &pem_key_cert_pair, nullptr
  ));

  return newChannelCredentialsObj;
}

Object HHVM_STATIC_METHOD(ChannelCredentials, createComposite,
                          const Object& cred1_obj,
                          const Object& cred2_obj)
{
    HHVM_TRACE_SCOPE("ChannelCredentials createComposite") // Degug Trace

  auto channelCredentialsData = Native::data<ChannelCredentialsData>(cred1_obj);
  auto callCredentialsData = Native::data<CallCredentialsData>(cred2_obj);

  grpc_channel_credentials* channel_credentials = grpc_composite_channel_credentials_create(
    channelCredentialsData->credentials(),
    callCredentialsData->credentials(),
    NULL
  );

  auto newChannelCredentialsObj = Object{ChannelCredentialsData::getClass()};
  auto newChannelCredentialsData = Native::data<ChannelCredentialsData>(newChannelCredentialsObj);
  newChannelCredentialsData->init(channel_credentials);
  newChannelCredentialsData->setHashKey(channelCredentialsData->getHashKey());

  return newChannelCredentialsObj;
}

Variant HHVM_STATIC_METHOD(ChannelCredentials, createInsecure)
{
    HHVM_TRACE_SCOPE("ChannelCredentials createInsecure") // Degug Trace

  return Variant();
}

void grpc_hhvm_init_channel_credentials()
{
  grpc_set_ssl_roots_override_callback(get_ssl_roots_override);
}

} // namespace HPHP
