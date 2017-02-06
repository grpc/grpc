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

#include <grpc++/test/server_context_test_spouse.h>

#include <cstring>
#include <vector>

#include <grpc++/impl/grpc_library.h>
#include <gtest/gtest.h>

namespace grpc {
namespace testing {

static internal::GrpcLibraryInitializer g_initializer;

const char key1[] = "metadata-key1";
const char key2[] = "metadata-key2";
const char val1[] = "metadata-val1";
const char val2[] = "metadata-val2";

bool ClientMetadataContains(const ServerContext& context,
                            const grpc::string_ref& key,
                            const grpc::string_ref& value) {
  const auto& client_metadata = context.client_metadata();
  for (auto iter = client_metadata.begin(); iter != client_metadata.end();
       ++iter) {
    if (iter->first == key && iter->second == value) {
      return true;
    }
  }
  return false;
}

TEST(ServerContextTestSpouseTest, ClientMetadata) {
  ServerContext context;
  ServerContextTestSpouse spouse(&context);

  spouse.AddClientMetadata(key1, val1);
  ASSERT_TRUE(ClientMetadataContains(context, key1, val1));

  spouse.AddClientMetadata(key2, val2);
  ASSERT_TRUE(ClientMetadataContains(context, key1, val1));
  ASSERT_TRUE(ClientMetadataContains(context, key2, val2));
}

TEST(ServerContextTestSpouseTest, InitialMetadata) {
  ServerContext context;
  ServerContextTestSpouse spouse(&context);
  std::multimap<grpc::string, grpc::string> metadata;

  context.AddInitialMetadata(key1, val1);
  metadata.insert(std::pair<grpc::string, grpc::string>(key1, val1));
  ASSERT_EQ(metadata, spouse.GetInitialMetadata());

  context.AddInitialMetadata(key2, val2);
  metadata.insert(std::pair<grpc::string, grpc::string>(key2, val2));
  ASSERT_EQ(metadata, spouse.GetInitialMetadata());
}

TEST(ServerContextTestSpouseTest, TrailingMetadata) {
  ServerContext context;
  ServerContextTestSpouse spouse(&context);
  std::multimap<grpc::string, grpc::string> metadata;

  context.AddTrailingMetadata(key1, val1);
  metadata.insert(std::pair<grpc::string, grpc::string>(key1, val1));
  ASSERT_EQ(metadata, spouse.GetTrailingMetadata());

  context.AddTrailingMetadata(key2, val2);
  metadata.insert(std::pair<grpc::string, grpc::string>(key2, val2));
  ASSERT_EQ(metadata, spouse.GetTrailingMetadata());
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
