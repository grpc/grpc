#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>
#include <list>
#include <string>
#include <utility>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/rls/rls.h"
#include "src/core/lib/iomgr/error.h"

namespace grpc_core {

namespace testing {

class RlsKeyBuilderTest : public ::testing::Test {
 protected:
  void CheckNoError(grpc_error* error, bool fatal = false) {
    if (error != GRPC_ERROR_NONE) {
      std::cout << "Found error: " << grpc_error_string(error) << std::endl;
    }
    if (fatal) {
      ASSERT_EQ(error, GRPC_ERROR_NONE);
    } else {
      EXPECT_EQ(error, GRPC_ERROR_NONE);
    }
  }

  void BuildJson(std::string json_string, Json* json) {
    grpc_error* error;
    *json = Json::Parse(json_string, &error);
    CheckNoError(error, true);
  }
};

const char* default_build_map_config =
    "["
    "  {"
    "    \"names\":["
    "      {"
    "        \"service\":\"test_service1\","
    "        \"method\":\"test_method1\""
    "      },"
    "      {"
    "        \"service\":\"test_service1\","
    "        \"method\":\"test_method2\""
    "      },"
    "      {"
    "        \"service\":\"test_service2\","
    "        \"method\":\"test_method1\""
    "      }"
    "    ],"
    "    \"headers\":["
    "      {"
    "        \"key\":\"key1\","
    "        \"names\":["
    "          \"key1_field1\","
    "          \"key1_field2\""
    "        ]"
    "      },"
    "      {"
    "        \"key\":\"key2\","
    "        \"names\":["
    "          \"key2_field1\","
    "          \"key2_field2\""
    "        ]"
    "      }"
    "    ]"
    "  },"
    "  {"
    "    \"names\":["
    "      {"
    "        \"service\":\"test_service2\","
    "        \"method\":\"test_method2\""
    "      }"
    "    ],"
    "    \"headers\":["
    "      {"
    "        \"key\":\"key3\","
    "        \"names\":["
    "          \"key3_field1\","
    "          \"key3_field2\""
    "        ]"
    "      }"
    "    ]"
    "  }"
    "]";

class TestMetadata : public LoadBalancingPolicy::MetadataInterface {
 public:
  void Add(StringView key, StringView value) override {
    metadata_.push_back({std::string(key), std::string(value)});
  }

  iterator begin() const override { return iterator(this, 0); }
  iterator end() const override { return iterator(this, metadata_.size()); }

  iterator erase(iterator it) override {
    int index = GetIteratorHandle(it);
    metadata_.erase(metadata_.begin() + index);
    return iterator(this, index);
  }

 private:
  intptr_t IteratorHandleNext(intptr_t handle) const override {
    return handle + 1;
  }

  std::pair<StringView, StringView> IteratorHandleGet(
      intptr_t handle) const override {
    return metadata_[handle];
  }

  std::vector<std::pair<std::string, std::string>> metadata_;
};

TEST_F(RlsKeyBuilderTest, ParseConfig) {
  Json config_json;
  grpc_error* error;

  BuildJson(default_build_map_config, &config_json);

  auto key_builder_map = RlsCreateKeyMapBuilderMap(config_json, &error);
  CheckNoError(error);
  EXPECT_EQ(key_builder_map.size(), 4);
  EXPECT_NE(key_builder_map.find("/test_service1/test_method1"),
            key_builder_map.end());
  EXPECT_NE(key_builder_map.find("/test_service1/test_method2"),
            key_builder_map.end());
  EXPECT_NE(key_builder_map.find("/test_service2/test_method1"),
            key_builder_map.end());
  EXPECT_NE(key_builder_map.find("/test_service2/test_method2"),
            key_builder_map.end());

  // Configs with conflicting service/method names.
  BuildJson(
      "["
      "  {"
      "    \"names\":["
      "      {"
      "        \"service\":\"test_service1\","
      "        \"method\":\"test_method1\""
      "      }"
      "    ],"
      "    \"headers\":["
      "      {"
      "        \"key\":\"key1\","
      "        \"names\":["
      "          \"key1_field1\","
      "          \"key1_field2\""
      "        ]"
      "      }"
      "    ]"
      "  },"
      "  {"
      "    \"names\":["
      "      {"
      "        \"service\":\"test_service1\","
      "        \"method\":\"test_method1\""
      "      }"
      "    ],"
      "    \"headers\":["
      "      {"
      "        \"key\":\"key3\","
      "        \"names\":["
      "          \"key3_field1\","
      "          \"key3_field2\""
      "        ]"
      "      }"
      "    ]"
      "  }"
      "]",
      &config_json);
  key_builder_map = RlsCreateKeyMapBuilderMap(config_json, &error);
  EXPECT_NE(error, GRPC_ERROR_NONE);

  // Configs with no service/method names.
  BuildJson(
      "["
      "  {"
      "    \"names\":["
      "    ],"
      "    \"headers\":["
      "      {"
      "        \"key\":\"key1\","
      "        \"names\":["
      "          \"key1_field1\","
      "          \"key1_field2\""
      "        ]"
      "      }"
      "    ]"
      "  }"
      "]",
      &config_json);
  key_builder_map = RlsCreateKeyMapBuilderMap(config_json, &error);
  EXPECT_NE(error, GRPC_ERROR_NONE);
}

TEST_F(RlsKeyBuilderTest, WildcardMatch) {
  Json config_json;
  BuildJson(
      "["
      "  {"
      "    \"names\":["
      "      {"
      "        \"service\":\"test_service1\","
      "        \"method\":\"*\""
      "      },"
      "      {"
      "        \"service\":\"test_service2\","
      "        \"method\":\"test_method1\""
      "      }"
      "    ],"
      "    \"headers\":["
      "      {"
      "        \"key\":\"key1\","
      "        \"names\":["
      "          \"key1_field1\","
      "          \"key1_field2\""
      "        ]"
      "      }"
      "    ]"
      "  }"
      "]",
      &config_json);

  grpc_error* error;
  auto key_builder_map = RlsCreateKeyMapBuilderMap(config_json, &error);
  CheckNoError(error, true);

  TestMetadata metadata;
  metadata.Add(":path", "/test_service1/some_random_method");
  auto path = RlsFindPathFromMetadata(&metadata);
  auto builder = RlsFindKeyMapBuilder(key_builder_map, path);
  EXPECT_NE(builder, nullptr);

  TestMetadata metadata2;
  metadata2.Add(":path", "/test_service2/some_random_method");
  path = RlsFindPathFromMetadata(&metadata2);
  builder = RlsFindKeyMapBuilder(key_builder_map, path);
  EXPECT_EQ(builder, nullptr);
}

TEST_F(RlsKeyBuilderTest, KeyExtraction) {
  Json config_json;
  grpc_error* error;

  BuildJson(default_build_map_config, &config_json);
  auto key_builder_map = RlsCreateKeyMapBuilderMap(config_json, &error);

  TestMetadata metadata;
  metadata.Add("key1_field1", "key1_val");
  metadata.Add("key2_field2", "key2_val");
  metadata.Add("key3_field1", "key3_val");
  metadata.Add(":path", "/test_service1/test_method2");
  auto path = RlsFindPathFromMetadata(&metadata);
  auto builder = RlsFindKeyMapBuilder(key_builder_map, path);
  ASSERT_NE(builder, nullptr);
  auto key = builder->BuildKeyMap(&metadata);
  EXPECT_EQ(key.size(), 2);
  EXPECT_EQ(key["key1"], "key1_val");
  EXPECT_EQ(key["key2"], "key2_val");

  TestMetadata metadata2;
  metadata2.Add("key1_field1", "key1_val");
  metadata2.Add("key2_field2", "key2_val");
  metadata2.Add("key3_field1", "key3_val");
  metadata2.Add(":path", "/test_service2/test_method2");
  path = RlsFindPathFromMetadata(&metadata2);
  builder = RlsFindKeyMapBuilder(key_builder_map, path);
  ASSERT_NE(builder, nullptr);
  key = builder->BuildKeyMap(&metadata2);
  EXPECT_EQ(key.size(), 1);
  EXPECT_EQ(key["key3"], "key3_val");

  // Test multiple identical fields.
  TestMetadata metadata3;
  metadata3.Add("key1_field1", "key1_val1");
  metadata3.Add("key2_field2", "key2_val");
  metadata3.Add("key1_field1", "key1_val2");
  metadata3.Add(":path", "/test_service1/test_method1");
  path = RlsFindPathFromMetadata(&metadata3);
  builder = RlsFindKeyMapBuilder(key_builder_map, path);
  ASSERT_NE(builder, nullptr);
  key = builder->BuildKeyMap(&metadata3);
  EXPECT_EQ(key.size(), 2);
  EXPECT_EQ(key["key1"], "key1_val1,key1_val2");
  EXPECT_EQ(key["key2"], "key2_val");
}

}  // namespace testing

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
