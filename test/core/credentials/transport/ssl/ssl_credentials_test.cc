//
//
// Copyright 2017 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/credentials/transport/ssl/ssl_credentials.h"

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <stdio.h>
#include <string.h>

#include "src/core/credentials/transport/tls/ssl_utils.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

using grpc_core::MakeRefCounted;
using grpc_core::RefCountedPtr;

TEST(SslCredentialsTest, ConvertGrpcToTsiCertPairs) {
  grpc_ssl_pem_key_cert_pair grpc_pairs[] = {{"private_key1", "cert_chain1"},
                                             {"private_key2", "cert_chain2"},
                                             {"private_key3", "cert_chain3"}};
  const size_t num_pairs = 3;

  {
    tsi_ssl_pem_key_cert_pair* tsi_pairs =
        grpc_convert_grpc_to_tsi_cert_pairs(grpc_pairs, 0);
    ASSERT_EQ(tsi_pairs, nullptr);
  }

  {
    tsi_ssl_pem_key_cert_pair* tsi_pairs =
        grpc_convert_grpc_to_tsi_cert_pairs(grpc_pairs, num_pairs);

    ASSERT_NE(tsi_pairs, nullptr);
    for (size_t i = 0; i < num_pairs; i++) {
      ASSERT_EQ(strncmp(grpc_pairs[i].private_key, tsi_pairs[i].private_key,
                        strlen(grpc_pairs[i].private_key)),
                0);
      ASSERT_EQ(strncmp(grpc_pairs[i].cert_chain, tsi_pairs[i].cert_chain,
                        strlen(grpc_pairs[i].cert_chain)),
                0);
    }

    grpc_tsi_ssl_pem_key_cert_pairs_destroy(tsi_pairs, num_pairs);
  }
}

class SslLeafHashComparatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ctx1_ = MakeRefCounted<grpc_auth_context>(nullptr);
    ctx2_ = MakeRefCounted<grpc_auth_context>(nullptr);
  }

  void TearDown() override {
    ctx1_.reset();
    ctx2_.reset();
  }

  RefCountedPtr<grpc_auth_context> ctx1_;
  RefCountedPtr<grpc_auth_context> ctx2_;
};

TEST_F(SslLeafHashComparatorTest, BothEmpty) {
  EXPECT_FALSE(SslLeafHashComparator(ctx1_.get(), ctx2_.get()));
}

TEST_F(SslLeafHashComparatorTest, OneEmpty) {
  grpc_auth_context_add_cstring_property(
      ctx1_.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME, "cert");
  EXPECT_FALSE(SslLeafHashComparator(ctx1_.get(), ctx2_.get()));
  EXPECT_FALSE(SslLeafHashComparator(ctx2_.get(), ctx1_.get()));
}

TEST_F(SslLeafHashComparatorTest, Match) {
  grpc_auth_context_add_cstring_property(
      ctx1_.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME, "cert");
  grpc_auth_context_add_cstring_property(
      ctx2_.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME, "cert");
  EXPECT_TRUE(SslLeafHashComparator(ctx1_.get(), ctx2_.get()));
}

TEST_F(SslLeafHashComparatorTest, Mismatch) {
  grpc_auth_context_add_cstring_property(
      ctx1_.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME, "cert1");
  grpc_auth_context_add_cstring_property(
      ctx2_.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME, "cert2");
  EXPECT_FALSE(SslLeafHashComparator(ctx1_.get(), ctx2_.get()));
}

TEST_F(SslLeafHashComparatorTest, IgnoresOtherProperties) {
  grpc_auth_context_add_cstring_property(
      ctx1_.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME, "cert");
  grpc_auth_context_add_cstring_property(ctx1_.get(), "other_prop", "val1");

  grpc_auth_context_add_cstring_property(
      ctx2_.get(), GRPC_X509_PEM_CERT_PROPERTY_NAME, "cert");
  grpc_auth_context_add_cstring_property(ctx2_.get(), "other_prop", "val2");

  EXPECT_TRUE(SslLeafHashComparator(ctx1_.get(), ctx2_.get()));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
