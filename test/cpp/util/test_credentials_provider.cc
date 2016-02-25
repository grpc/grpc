
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

#include <grpc/support/sync.h>
#include <grpc++/impl/sync.h>

#include "test/core/end2end/data/ssl_test_data.h"

namespace {

using grpc::ChannelArguments;
using grpc::ChannelCredentials;
using grpc::InsecureChannelCredentials;
using grpc::InsecureServerCredentials;
using grpc::ServerCredentials;
using grpc::SslCredentialsOptions;
using grpc::SslServerCredentialsOptions;
using grpc::testing::CredentialsProvider;

class DefaultCredentialsProvider : public CredentialsProvider {
 public:
  ~DefaultCredentialsProvider() override {}

  std::shared_ptr<ChannelCredentials> GetChannelCredentials(
      const grpc::string& type, ChannelArguments* args) override {
    if (type == grpc::testing::kInsecureCredentialsType) {
      return InsecureChannelCredentials();
    } else if (type == grpc::testing::kTlsCredentialsType) {
      SslCredentialsOptions ssl_opts = {test_root_cert, "", ""};
      args->SetSslTargetNameOverride("foo.test.google.fr");
      return SslCredentials(ssl_opts);
    } else {
      gpr_log(GPR_ERROR, "Unsupported credentials type %s.", type.c_str());
    }
    return nullptr;
  }

  std::shared_ptr<ServerCredentials> GetServerCredentials(
      const grpc::string& type) override {
    if (type == grpc::testing::kInsecureCredentialsType) {
      return InsecureServerCredentials();
    } else if (type == grpc::testing::kTlsCredentialsType) {
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
  std::vector<grpc::string> GetSecureCredentialsTypeList() override {
    std::vector<grpc::string> types;
    types.push_back(grpc::testing::kTlsCredentialsType);
    return types;
  }
};

gpr_once g_once_init_provider_mu = GPR_ONCE_INIT;
grpc::mutex* g_provider_mu = nullptr;
CredentialsProvider* g_provider = nullptr;

void InitProviderMu() { g_provider_mu = new grpc::mutex; }

grpc::mutex& GetMu() {
  gpr_once_init(&g_once_init_provider_mu, &InitProviderMu);
  return *g_provider_mu;
}

CredentialsProvider* GetProvider() {
  grpc::unique_lock<grpc::mutex> lock(GetMu());
  if (g_provider == nullptr) {
    g_provider = new DefaultCredentialsProvider;
  }
  return g_provider;
}

}  // namespace

namespace grpc {
namespace testing {

// Note that it is not thread-safe to set a provider while concurrently using
// the previously set provider, as this deletes and replaces it. nullptr may be
// given to reset to the default.
void SetTestCredentialsProvider(std::unique_ptr<CredentialsProvider> provider) {
  grpc::unique_lock<grpc::mutex> lock(GetMu());
  if (g_provider != nullptr) {
    delete g_provider;
  }
  g_provider = provider.release();
}

std::shared_ptr<ChannelCredentials> GetChannelCredentials(
    const grpc::string& type, ChannelArguments* args) {
  return GetProvider()->GetChannelCredentials(type, args);
}

std::shared_ptr<ServerCredentials> GetServerCredentials(
    const grpc::string& type) {
  return GetProvider()->GetServerCredentials(type);
}

std::vector<grpc::string> GetSecureCredentialsTypeList() {
  return GetProvider()->GetSecureCredentialsTypeList();
}

}  // namespace testing
}  // namespace grpc
