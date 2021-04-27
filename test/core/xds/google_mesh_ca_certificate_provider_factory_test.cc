//
//
// Copyright 2020 gRPC authors.
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "src/core/ext/xds/google_mesh_ca_certificate_provider_factory.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(GoogleMeshCaConfigTest, Basic) {
  const char* json_str =
      "{"
      "  \"server\": {"
      "    \"api_type\": \"GRPC\","
      "    \"grpc_services\": [{"
      "      \"google_grpc\": {"
      "        \"target_uri\": \"newmeshca.googleapis.com\","
      "        \"channel_credentials\": { \"google_default\": {}},"
      "        \"call_credentials\": [{"
      "          \"sts_service\": {"
      "            \"token_exchange_service_uri\": "
      "\"newsecuretoken.googleapis.com\","
      "            \"resource\": \"newmeshca.googleapis.com\","
      "            \"audience\": \"newmeshca.googleapis.com\","
      "            \"scope\": "
      "\"https://www.newgoogleapis.com/auth/cloud-platform\","
      "            \"requested_token_type\": "
      "\"urn:ietf:params:oauth:token-type:jwt\","
      "            \"subject_token_path\": \"/etc/secret/sajwt.token\","
      "            \"subject_token_type\": "
      "\"urn:ietf:params:oauth:token-type:jwt\","
      "            \"actor_token_path\": \"/etc/secret/sajwt.token\","
      "            \"actor_token_type\": "
      "\"urn:ietf:params:oauth:token-type:jwt\""
      "          }"
      "        }]"
      "      },"
      "      \"timeout\": \"20s\""
      "    }]"
      "  },"
      "  \"certificate_lifetime\": \"400s\","
      "  \"renewal_grace_period\": \"100s\","
      "  \"key_type\": \"RSA\","
      "  \"key_size\": 1024,"
      "  \"location\": "
      "\"https://container.googleapis.com/v1/project/test-project1/locations/"
      "test-zone2/clusters/test-cluster3\""
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  auto config =
      GoogleMeshCaCertificateProviderFactory::Config::Parse(json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  EXPECT_EQ(config->endpoint(), "newmeshca.googleapis.com");
  EXPECT_EQ(config->sts_config().token_exchange_service_uri,
            "newsecuretoken.googleapis.com");
  EXPECT_EQ(config->sts_config().resource, "newmeshca.googleapis.com");
  EXPECT_EQ(config->sts_config().audience, "newmeshca.googleapis.com");
  EXPECT_EQ(config->sts_config().scope,
            "https://www.newgoogleapis.com/auth/cloud-platform");
  EXPECT_EQ(config->sts_config().requested_token_type,
            "urn:ietf:params:oauth:token-type:jwt");
  EXPECT_EQ(config->sts_config().subject_token_path, "/etc/secret/sajwt.token");
  EXPECT_EQ(config->sts_config().subject_token_type,
            "urn:ietf:params:oauth:token-type:jwt");
  EXPECT_EQ(config->sts_config().actor_token_path, "/etc/secret/sajwt.token");
  EXPECT_EQ(config->sts_config().actor_token_type,
            "urn:ietf:params:oauth:token-type:jwt");
  EXPECT_EQ(config->timeout(), 20 * 1000);
  EXPECT_EQ(config->certificate_lifetime(), 400 * 1000);
  EXPECT_EQ(config->renewal_grace_period(), 100 * 1000);
  EXPECT_EQ(config->key_size(), 1024);
  EXPECT_EQ(config->location(),
            "https://container.googleapis.com/v1/project/test-project1/"
            "locations/test-zone2/clusters/test-cluster3");
}

TEST(GoogleMeshCaConfigTest, Defaults) {
  const char* json_str =
      "{"
      "  \"server\": {"
      "    \"api_type\": \"GRPC\","
      "    \"grpc_services\": [{"
      "      \"google_grpc\": {"
      "        \"call_credentials\": [{"
      "          \"sts_service\": {"
      "            \"scope\": "
      "\"https://www.googleapis.com/auth/cloud-platform\","
      "            \"subject_token_path\": \"/etc/secret/sajwt.token\","
      "            \"subject_token_type\": "
      "\"urn:ietf:params:oauth:token-type:jwt\""
      "          }"
      "        }]"
      "      }"
      "    }]"
      "  },"
      "  \"location\": "
      "\"https://container.googleapis.com/v1/project/test-project1/locations/"
      "test-zone2/clusters/test-cluster3\""
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  auto config =
      GoogleMeshCaCertificateProviderFactory::Config::Parse(json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  EXPECT_EQ(config->endpoint(), "meshca.googleapis.com");
  EXPECT_EQ(config->sts_config().token_exchange_service_uri,
            "securetoken.googleapis.com");
  EXPECT_EQ(config->sts_config().resource, "");
  EXPECT_EQ(config->sts_config().audience, "");
  EXPECT_EQ(config->sts_config().scope,
            "https://www.googleapis.com/auth/cloud-platform");
  EXPECT_EQ(config->sts_config().requested_token_type, "");
  EXPECT_EQ(config->sts_config().subject_token_path, "/etc/secret/sajwt.token");
  EXPECT_EQ(config->sts_config().subject_token_type,
            "urn:ietf:params:oauth:token-type:jwt");
  EXPECT_EQ(config->sts_config().actor_token_path, "");
  EXPECT_EQ(config->sts_config().actor_token_type, "");
  EXPECT_EQ(config->timeout(), 10 * 1000);
  EXPECT_EQ(config->certificate_lifetime(), 24 * 60 * 60 * 1000);
  EXPECT_EQ(config->renewal_grace_period(), 12 * 60 * 60 * 1000);
  EXPECT_EQ(config->key_size(), 2048);
  EXPECT_EQ(config->location(),
            "https://container.googleapis.com/v1/project/test-project1/"
            "locations/test-zone2/clusters/test-cluster3");
}

TEST(GoogleMeshCaConfigTest, WrongExpectedValues) {
  const char* json_str =
      "{"
      "  \"server\": {"
      "    \"api_type\": \"REST\","
      "    \"grpc_services\": [{"
      "      \"google_grpc\": {"
      "        \"call_credentials\": [{"
      "          \"sts_service\": {"
      "            \"scope\": "
      "\"https://www.googleapis.com/auth/cloud-platform\","
      "            \"subject_token_path\": \"/etc/secret/sajwt.token\","
      "            \"subject_token_type\": "
      "\"urn:ietf:params:oauth:token-type:jwt\""
      "          }"
      "        }]"
      "      }"
      "    }]"
      "  },"
      "  \"key_type\": \"DSA\","
      "  \"location\": "
      "\"https://container.googleapis.com/v1/project/test-project1/locations/"
      "test-zone2/clusters/test-cluster3\""
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  auto config =
      GoogleMeshCaCertificateProviderFactory::Config::Parse(json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("field:api_type error:Only GRPC is supported.*"
                               "field:key_type error:Only RSA is supported"));
  GRPC_ERROR_UNREF(error);
}

TEST(GoogleMeshCaConfigTest, WrongTypes) {
  const char* json_str =
      "{"
      "  \"server\": {"
      "    \"api_type\": 123,"
      "    \"grpc_services\": [{"
      "      \"google_grpc\": {"
      "        \"target_uri\": 123,"
      "        \"call_credentials\": [{"
      "          \"sts_service\": {"
      "            \"token_exchange_service_uri\": 123,"
      "            \"resource\": 123,"
      "            \"audience\": 123,"
      "            \"scope\": 123,"
      "            \"requested_token_type\": 123,"
      "            \"subject_token_path\": 123,"
      "            \"subject_token_type\": 123,"
      "            \"actor_token_path\": 123,"
      "            \"actor_token_type\": 123"
      "          }"
      "        }]"
      "      },"
      "      \"timeout\": 20"
      "    }]"
      "  },"
      "  \"certificate_lifetime\": 400,"
      "  \"renewal_grace_period\": 100,"
      "  \"key_type\": 123,"
      "  \"key_size\": \"1024\","
      "  \"location\": 123"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  auto config =
      GoogleMeshCaCertificateProviderFactory::Config::Parse(json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex(
          "field:server.*field:api_type error:type should be STRING.*"
          "field:grpc_services.*field:google_grpc.*field:target_uri "
          "error:type should be STRING.*"
          "field:call_credentials.*field:sts_service.*field:token_exchange_"
          "service_uri error:type should be STRING.*"
          "field:resource error:type should be STRING.*"
          "field:audience error:type should be STRING.*"
          "field:scope error:type should be STRING.*"
          "field:requested_token_type error:type should be STRING.*"
          "field:subject_token_path error:type should be STRING.*"
          "field:subject_token_type error:type should be STRING.*"
          "field:actor_token_path error:type should be STRING.*"
          "field:actor_token_type error:type should be STRING.*"
          "field:timeout error:type should be STRING of the form given by "
          "google.proto.Duration.*"
          "field:certificate_lifetime error:type should be STRING of the form "
          "given by google.proto.Duration.*"
          "field:renewal_grace_period error:type should be STRING of the form "
          "given by google.proto.Duration..*"
          "field:key_type error:type should be STRING.*"
          "field:key_size error:type should be NUMBER.*"
          "field:location error:type should be STRING"));
  GRPC_ERROR_UNREF(error);
}

TEST(GoogleMeshCaConfigTest, GrpcServicesNotAnArray) {
  const char* json_str =
      "{"
      "  \"server\": {"
      "    \"api_type\": \"GRPC\","
      "    \"grpc_services\": 123"
      "  },"
      "  \"location\": "
      "\"https://container.googleapis.com/v1/project/test-project1/locations/"
      "test-zone2/clusters/test-cluster3\""
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  auto config =
      GoogleMeshCaCertificateProviderFactory::Config::Parse(json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex(
          "field:server.*field:grpc_services error:type should be ARRAY"));
  GRPC_ERROR_UNREF(error);
}

TEST(GoogleMeshCaConfigTest, GoogleGrpcNotAnObject) {
  const char* json_str =
      "{"
      "  \"server\": {"
      "    \"api_type\": \"GRPC\","
      "    \"grpc_services\": [{"
      "      \"google_grpc\": 123"
      "    }]"
      "  },"
      "  \"location\": "
      "\"https://container.googleapis.com/v1/project/test-project1/locations/"
      "test-zone2/clusters/test-cluster3\""
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  auto config =
      GoogleMeshCaCertificateProviderFactory::Config::Parse(json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("field:server.*field:grpc_services.*field:"
                               "google_grpc error:type should be OBJECT"));
  GRPC_ERROR_UNREF(error);
}

TEST(GoogleMeshCaConfigTest, CallCredentialsNotAnArray) {
  const char* json_str =
      "{"
      "  \"server\": {"
      "    \"api_type\": \"GRPC\","
      "    \"grpc_services\": [{"
      "      \"google_grpc\": {"
      "        \"call_credentials\": 123"
      "      }"
      "    }]"
      "  },"
      "  \"location\": "
      "\"https://container.googleapis.com/v1/project/test-project1/locations/"
      "test-zone2/clusters/test-cluster3\""
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  auto config =
      GoogleMeshCaCertificateProviderFactory::Config::Parse(json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "field:server.*field:grpc_services.*field:google_grpc.*"
                  "field:call_credentials error:type should be ARRAY"));
  GRPC_ERROR_UNREF(error);
}

TEST(GoogleMeshCaConfigTest, StsServiceNotAnObject) {
  const char* json_str =
      "{"
      "  \"server\": {"
      "    \"api_type\": \"GRPC\","
      "    \"grpc_services\": [{"
      "      \"google_grpc\": {"
      "        \"call_credentials\": [{"
      "          \"sts_service\": 123"
      "        }]"
      "      }"
      "    }]"
      "  },"
      "  \"location\": "
      "\"https://container.googleapis.com/v1/project/test-project1/locations/"
      "test-zone2/clusters/test-cluster3\""
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  auto config =
      GoogleMeshCaCertificateProviderFactory::Config::Parse(json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex(
          "field:server.*field:grpc_services.*field:google_grpc.*field:"
          "call_credentials.*field:sts_service error:type should be OBJECT"));
  GRPC_ERROR_UNREF(error);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
