
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

#include <unordered_map>

#include <grpc++/impl/sync.h>
#include <grpc/support/sync.h>

#include "test/core/end2end/data/ssl_test_data.h"

namespace {

using grpc::ChannelArguments;
using grpc::ChannelCredentials;
using grpc::InsecureChannelCredentials;
using grpc::InsecureServerCredentials;
using grpc::ServerCredentials;
using grpc::SslCredentialsOptions;
using grpc::SslServerCredentialsOptions;
using grpc::testing::CredentialTypeProvider;

// Provide test credentials. Thread-safe.
class CredentialsProvider {
 public:
  virtual ~CredentialsProvider() {}

  virtual void AddSecureType(
      const grpc::string& type,
      std::unique_ptr<CredentialTypeProvider> type_provider) = 0;
  virtual std::shared_ptr<ChannelCredentials> GetChannelCredentials(
      const grpc::string& type, ChannelArguments* args) = 0;
  virtual std::shared_ptr<ServerCredentials> GetServerCredentials(
      const grpc::string& type) = 0;
  virtual std::vector<grpc::string> GetSecureCredentialsTypeList() = 0;
};

class DefaultCredentialsProvider : public CredentialsProvider {
 public:
  ~DefaultCredentialsProvider() GRPC_OVERRIDE {}

  void AddSecureType(const grpc::string& type,
                     std::unique_ptr<CredentialTypeProvider> type_provider)
      GRPC_OVERRIDE {
    // This clobbers any existing entry for type, except the defaults, which
    // can't be clobbered.
    grpc::unique_lock<grpc::mutex> lock(mu_);
    auto it = std::find(added_secure_type_names_.begin(),
                        added_secure_type_names_.end(), type);
    if (it == added_secure_type_names_.end()) {
      added_secure_type_names_.push_back(type);
      added_secure_type_providers_.push_back(std::move(type_provider));
    } else {
      added_secure_type_providers_[it - added_secure_type_names_.begin()] =
          std::move(type_provider);
    }
  }

  std::shared_ptr<ChannelCredentials> GetChannelCredentials(
      const grpc::string& type, ChannelArguments* args) GRPC_OVERRIDE {
    if (type == grpc::testing::kInsecureCredentialsType) {
      return InsecureChannelCredentials();
    } else if (type == grpc::testing::kTlsCredentialsType) {
      SslCredentialsOptions ssl_opts = {test_root_cert, "", ""};
      args->SetSslTargetNameOverride("foo.test.google.fr");
      return SslCredentials(ssl_opts);
    } else {
      grpc::unique_lock<grpc::mutex> lock(mu_);
      auto it(std::find(added_secure_type_names_.begin(),
                        added_secure_type_names_.end(), type));
      if (it == added_secure_type_names_.end()) {
        gpr_log(GPR_ERROR, "Unsupported credentials type %s.", type.c_str());
        return nullptr;
      }
      return added_secure_type_providers_[it - added_secure_type_names_.begin()]
          ->GetChannelCredentials(args);
    }
  }

  std::shared_ptr<ServerCredentials> GetServerCredentials(
      const grpc::string& type) GRPC_OVERRIDE {
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
      grpc::unique_lock<grpc::mutex> lock(mu_);
      auto it(std::find(added_secure_type_names_.begin(),
                        added_secure_type_names_.end(), type));
      if (it == added_secure_type_names_.end()) {
        gpr_log(GPR_ERROR, "Unsupported credentials type %s.", type.c_str());
        return nullptr;
      }
      return added_secure_type_providers_[it - added_secure_type_names_.begin()]
          ->GetServerCredentials();
    }
  }
  std::vector<grpc::string> GetSecureCredentialsTypeList() GRPC_OVERRIDE {
    std::vector<grpc::string> types;
    types.push_back(grpc::testing::kTlsCredentialsType);
    grpc::unique_lock<grpc::mutex> lock(mu_);
    for (auto it = added_secure_type_names_.begin();
         it != added_secure_type_names_.end(); it++) {
      types.push_back(*it);
    }
    return types;
  }

 private:
  grpc::mutex mu_;
  std::vector<grpc::string> added_secure_type_names_;
  std::vector<std::unique_ptr<CredentialTypeProvider>>
      added_secure_type_providers_;
};

gpr_once g_once_init_provider = GPR_ONCE_INIT;
CredentialsProvider* g_provider = nullptr;

void CreateDefaultProvider() { g_provider = new DefaultCredentialsProvider; }

CredentialsProvider* GetProvider() {
  gpr_once_init(&g_once_init_provider, &CreateDefaultProvider);
  return g_provider;
}

}  // namespace

namespace grpc {
namespace testing {

void AddSecureType(const grpc::string& type,
                   std::unique_ptr<CredentialTypeProvider> type_provider) {
  GetProvider()->AddSecureType(type, std::move(type_provider));
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
