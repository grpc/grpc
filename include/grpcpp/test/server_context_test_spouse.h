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

#ifndef GRPCPP_TEST_SERVER_CONTEXT_TEST_SPOUSE_H
#define GRPCPP_TEST_SERVER_CONTEXT_TEST_SPOUSE_H

#include <map>

#include <grpcpp/server_context.h>

namespace grpc {
namespace testing {

/// A test-only class to access private members and methods of ServerContext.
class ServerContextTestSpouse {
 public:
  explicit ServerContextTestSpouse(ServerContext* ctx) : ctx_(ctx) {}

  /// Inject client metadata to the ServerContext for the test. The test spouse
  /// must be alive when \a ServerContext::client_metadata is called.
  void AddClientMetadata(const grpc::string& key, const grpc::string& value) {
    client_metadata_storage_.insert(
        std::pair<grpc::string, grpc::string>(key, value));
    ctx_->client_metadata_.map()->clear();
    for (auto iter = client_metadata_storage_.begin();
         iter != client_metadata_storage_.end(); ++iter) {
      ctx_->client_metadata_.map()->insert(
          std::pair<grpc::string_ref, grpc::string_ref>(
              iter->first.c_str(),
              grpc::string_ref(iter->second.data(), iter->second.size())));
    }
  }

  std::multimap<grpc::string, grpc::string> GetInitialMetadata() const {
    return ctx_->initial_metadata_;
  }

  std::multimap<grpc::string, grpc::string> GetTrailingMetadata() const {
    return ctx_->trailing_metadata_;
  }

 private:
  ServerContext* ctx_;  // not owned
  std::multimap<grpc::string, grpc::string> client_metadata_storage_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPCPP_TEST_SERVER_CONTEXT_TEST_SPOUSE_H
