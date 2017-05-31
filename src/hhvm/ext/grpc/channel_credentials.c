/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "channel_credentials.h"
#include "call_credentials.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hhvm_grpc.h"

#include <grpc/support/alloc.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

/**
 * Set default roots pem.
 * @param string $pem_roots PEM encoding of the server root certificates
 * @return void
 */
void HHVM_METHOD(ChannelCredentials, setDefaultRootsPem,
  const String& pem_roots) {
  ...
}

/**
 * Create a default channel credentials object.
 * @return ChannelCredentials The new default channel credentials object
 */
ChannelCredentials& HHVM_METHOD(ChannelCredentials, createDefault) {
  ...
}

/**
 * Create SSL credentials.
 * @param string $pem_root_certs PEM encoding of the server root certificates
 * @param string $pem_key_cert_pair.private_key PEM encoding of the client's
 *                                              private key (optional)
 * @param string $pem_key_cert_pair.cert_chain PEM encoding of the client's
 *                                             certificate chain (optional)
 * @return ChannelCredentials The new SSL credentials object
 */
ChannelCredentials& HHVM_METHOD(ChannelCredentials, createSsl,
  const String& pem_root_certs,
  const String& pem_key_cert_pair_private_key /*= null*/,
  const String& pem_key_cert_pair_cert_chain /*=null*/
  ) {
  ...
}

/**
 * Create composite credentials from two existing credentials.
 * @param ChannelCredentials $cred1_obj The first credential
 * @param CallCredentials $cred2_obj The second credential
 * @return ChannelCredentials The new composite credentials object
 */
ChannelCredentials& HHVM_METHOD(ChannelCredentials, createComposite,
  ChannelCredentials& cred1_obj,
  CallCredentials& cred2_obj) {
  ...
}

/**
 * Create insecure channel credentials
 * @return null
 */
void HHVM_METHOD(ChannelCredentials, createInsecure) {
  ...
}
