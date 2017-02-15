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

#ifndef GRPCXX_TEST_SERVER_CONTEXT_TEST_SPOUSE_H
#define GRPCXX_TEST_SERVER_CONTEXT_TEST_SPOUSE_H

#include <map>

#include <grpc++/server_context.h>

namespace grpc {
namespace testing {

// A test-only class to access private members and methods of ServerContext.
class ServerContextTestSpouse {
 public:
  explicit ServerContextTestSpouse(ServerContext* ctx) : ctx_(ctx) {}

  // Inject client metadata to the ServerContext for the test. The test spouse
  // must be alive when ServerContext::client_metadata is called.
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

#endif  // GRPCXX_TEST_SERVER_CONTEXT_TEST_SPOUSE_H
