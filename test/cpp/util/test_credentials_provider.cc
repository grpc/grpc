
/*
 *
 * Copyright 2016, Google Inc.
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

#include "test/cpp/util/test_credentials_provider.h"

#include "test/core/end2end/data/ssl_test_data.h"

namespace grpc {
namespace testing {

std::shared_ptr<ChannelCredentials> GetChannelCredentials(
    const grpc::string& type, ChannelArguments* args) {
  if (type == kInsecureCredentialsType) {
    return InsecureChannelCredentials();
  } else if (type == kTlsCredentialsType) {
    SslCredentialsOptions ssl_opts = {test_root_cert, "", ""};
    args->SetSslTargetNameOverride("foo.test.google.fr");
    return SslCredentials(ssl_opts);
  } else {
    gpr_log(GPR_ERROR, "Unsupported credentials type %s.", type.c_str());
  }
  return nullptr;
}

std::shared_ptr<ServerCredentials> GetServerCredentials(
    const grpc::string& type) {
  if (type == kInsecureCredentialsType) {
    return InsecureServerCredentials();
  } else if (type == kTlsCredentialsType) {
    SslServerCredentialsOptions::PemKeyCertPair pkcp = {test_server1_key,
                                                        test_server1_cert};
    SslServerCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = "";
    ssl_opts.pem_key_cert_pairs.push_back(pkcp);
    return SslServerCredentials(ssl_opts);
  } else {
    gpr_log(GPR_ERROR, "Unsupported credentials type %s.", type.c_str());
  }
  return nullptr;
}

}  // namespace testing
}  // namespace grpc
