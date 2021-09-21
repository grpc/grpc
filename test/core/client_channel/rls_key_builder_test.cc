#include <iostream>
#include <list>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/rls/rls.h"
#include "src/core/lib/iomgr/error.h"

namespace grpc_core {

class RlsKeyBuilderTest : public ::testing::Test {
 protected:
  Json BuildJson(std::string json_string) {
    grpc_error* error = GRPC_ERROR_NONE;
    Json result = Json::Parse(json_string, &error);
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    return result;
  }
};

const char* kDefaultBuildMapConfig =
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
  void Add(absl::string_view key, absl::string_view value) override {
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

  std::pair<absl::string_view, absl::string_view> IteratorHandleGet(
      intptr_t handle) const override {
    return metadata_[handle];
  }

  std::vector<std::pair<std::string, std::string>> metadata_;
};

TEST_F(RlsKeyBuilderTest, ParseConfigOk) {
  Json config_json = BuildJson(kDefaultBuildMapConfig);
  grpc_error* error = GRPC_ERROR_NONE;
  RlsLb::KeyMapBuilderMap key_builder_map =
      RlsCreateKeyMapBuilderMap(config_json, &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  EXPECT_THAT(
      key_builder_map,
      ::testing::ElementsAre(
          ::testing::Pair("/test_service1/test_method1", ::testing::_),
          ::testing::Pair("/test_service1/test_method2", ::testing::_),
          ::testing::Pair("/test_service2/test_method1", ::testing::_),
          ::testing::Pair("/test_service2/test_method2", ::testing::_)));
}

// Configs with conflicting service/method names.
TEST_F(RlsKeyBuilderTest, ParseConfigConflictingPath) {
  Json config_json = BuildJson(
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
      "]");
  grpc_error* error = GRPC_ERROR_NONE;
  RlsLb::KeyMapBuilderMap key_builder_map =
      RlsCreateKeyMapBuilderMap(config_json, &error);
  EXPECT_NE(error, GRPC_ERROR_NONE);
  EXPECT_THAT(
      grpc_error_string(error),
      ::testing::HasSubstr("duplicate name /test_service1/test_method1"));
}

// Configs with no service/method names.
TEST_F(RlsKeyBuilderTest, ParseConfigNoPath) {
  Json config_json = BuildJson(
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
      "]");
  grpc_error* error = GRPC_ERROR_NONE;
  RlsLb::KeyMapBuilderMap key_builder_map =
      RlsCreateKeyMapBuilderMap(config_json, &error);
  EXPECT_NE(error, GRPC_ERROR_NONE);
  EXPECT_THAT(grpc_error_string(error),
              ::testing::HasSubstr("\"names\" field is empty"));
}

TEST_F(RlsKeyBuilderTest, WildcardMatch) {
  Json config_json = BuildJson(
      "["
      "  {"
      "    \"names\":["
      "      {"
      "        \"service\":\"test_service1\""
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
      "]");
  grpc_error* error = GRPC_ERROR_NONE;
  RlsLb::KeyMapBuilderMap key_builder_map =
      RlsCreateKeyMapBuilderMap(config_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  const RlsLb::KeyMapBuilder* builder = RlsFindKeyMapBuilder(
      key_builder_map, "/test_service1/some_random_method");
  EXPECT_NE(builder, nullptr);
}

TEST_F(RlsKeyBuilderTest, WildcardNonMatch) {
  Json config_json = BuildJson(
      "["
      "  {"
      "    \"names\":["
      "      {"
      "        \"service\":\"test_service1\""
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
      "]");
  grpc_error* error = GRPC_ERROR_NONE;
  RlsLb::KeyMapBuilderMap key_builder_map =
      RlsCreateKeyMapBuilderMap(config_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  const RlsLb::KeyMapBuilder* builder = RlsFindKeyMapBuilder(
      key_builder_map, "/test_service2/some_random_method");
  EXPECT_EQ(builder, nullptr);
}

TEST_F(RlsKeyBuilderTest, KeyExtraction) {
  Json config_json = BuildJson(kDefaultBuildMapConfig);
  grpc_error* error = GRPC_ERROR_NONE;
  auto key_builder_map = RlsCreateKeyMapBuilderMap(config_json, &error);
  TestMetadata metadata;
  metadata.Add("key1_field1", "key1_val");
  metadata.Add("key2_field2", "key2_val");
  metadata.Add("key3_field1", "key3_val");
  const RlsLb::KeyMapBuilder* builder =
      RlsFindKeyMapBuilder(key_builder_map, "/test_service1/test_method2");
  ASSERT_NE(builder, nullptr);
  RlsLb::KeyMap key = builder->BuildKeyMap(&metadata);
  EXPECT_THAT(key, ::testing::ElementsAre(::testing::Pair("key1", "key1_val"),
                                          ::testing::Pair("key2", "key2_val")));
}

TEST_F(RlsKeyBuilderTest, PathMatching) {
  Json config_json = BuildJson(kDefaultBuildMapConfig);
  grpc_error* error = GRPC_ERROR_NONE;
  auto key_builder_map = RlsCreateKeyMapBuilderMap(config_json, &error);
  TestMetadata metadata;
  metadata.Add("key1_field1", "key1_val");
  metadata.Add("key2_field2", "key2_val");
  metadata.Add("key3_field1", "key3_val");
  const RlsLb::KeyMapBuilder* builder =
      RlsFindKeyMapBuilder(key_builder_map, "/test_service2/test_method2");
  ASSERT_NE(builder, nullptr);
  RlsLb::KeyMap key = builder->BuildKeyMap(&metadata);
  EXPECT_THAT(key, ::testing::ElementsAre(::testing::Pair("key3", "key3_val")));
}

TEST_F(RlsKeyBuilderTest, KeyExtractionMultipleIdenticalHeader) {
  Json config_json = BuildJson(kDefaultBuildMapConfig);
  grpc_error* error = GRPC_ERROR_NONE;
  auto key_builder_map = RlsCreateKeyMapBuilderMap(config_json, &error);
  TestMetadata metadata;
  metadata.Add("key1_field1", "key1_val1");
  metadata.Add("key1_field1", "key1_val2");
  const RlsLb::KeyMapBuilder* builder =
      RlsFindKeyMapBuilder(key_builder_map, "/test_service1/test_method1");
  ASSERT_NE(builder, nullptr);
  RlsLb::KeyMap key = builder->BuildKeyMap(&metadata);
  EXPECT_THAT(key, ::testing::ElementsAre(
                       ::testing::Pair("key1", "key1_val1,key1_val2")));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
