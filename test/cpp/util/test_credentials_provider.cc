
/*
 *
 * Copyright 2016 gRPC authors.
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

#include "test/cpp/util/test_credentials_provider.h"

#include <mutex>
#include <unordered_map>

#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "test/core/end2end/data/ssl_test_data.h"

namespace grpc {
namespace testing {
namespace {

class DefaultCredentialsProvider : public CredentialsProvider {
 public:
  ~DefaultCredentialsProvider() override {}

  void AddSecureType(
      const grpc::string& type,
      std::unique_ptr<CredentialTypeProvider> type_provider) override {
    // This clobbers any existing entry for type, except the defaults, which
    // can't be clobbered.
    std::unique_lock<std::mutex> lock(mu_);
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
      const grpc::string& type, ChannelArguments* args) override {
    if (type == grpc::testing::kInsecureCredentialsType) {
      return InsecureChannelCredentials();
    } else if (type == grpc::testing::kAltsCredentialsType) {
      grpc::experimental::AltsCredentialsOptions alts_opts;
      return grpc::experimental::AltsCredentials(alts_opts);
    } else if (type == grpc::testing::kTlsCredentialsType) {
      SslCredentialsOptions ssl_opts = {test_root_cert, "", ""};
      args->SetSslTargetNameOverride("foo.test.google.fr");
      return SslCredentials(ssl_opts);
    } else {
      std::unique_lock<std::mutex> lock(mu_);
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
      const grpc::string& type) override {
    if (type == grpc::testing::kInsecureCredentialsType) {
      return InsecureServerCredentials();
    } else if (type == grpc::testing::kAltsCredentialsType) {
      grpc::experimental::AltsServerCredentialsOptions alts_opts;
      return grpc::experimental::AltsServerCredentials(alts_opts);
    } else if (type == grpc::testing::kTlsCredentialsType) {
      SslServerCredentialsOptions::PemKeyCertPair pkcp = {test_server1_key,
                                                          test_server1_cert};
      SslServerCredentialsOptions ssl_opts;
      ssl_opts.pem_root_certs = "";
      ssl_opts.pem_key_cert_pairs.push_back(pkcp);
      return SslServerCredentials(ssl_opts);
    } else {
      std::unique_lock<std::mutex> lock(mu_);
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
  std::vector<grpc::string> GetSecureCredentialsTypeList() override {
    std::vector<grpc::string> types;
    types.push_back(grpc::testing::kTlsCredentialsType);
    std::unique_lock<std::mutex> lock(mu_);
    for (auto it = added_secure_type_names_.begin();
         it != added_secure_type_names_.end(); it++) {
      types.push_back(*it);
    }
    return types;
  }

 private:
  std::mutex mu_;
  std::vector<grpc::string> added_secure_type_names_;
  std::vector<std::unique_ptr<CredentialTypeProvider>>
      added_secure_type_providers_;
};

CredentialsProvider* g_provider = nullptr;

}  // namespace

CredentialsProvider* GetCredentialsProvider() {
  if (g_provider == nullptr) {
    g_provider = new DefaultCredentialsProvider;
  }
  return g_provider;
}

void SetCredentialsProvider(CredentialsProvider* provider) {
  // For now, forbids overriding provider.
  GPR_ASSERT(g_provider == nullptr);
  g_provider = provider;
}

}  // namespace testing
}  // namespace grpc
