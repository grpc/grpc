// Copyright 2024 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/http2_settings.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {

TEST(Http2SettingsTest, CanSetAndRetrieveSettings) {
  Http2Settings settings;
  settings.SetHeaderTableSize(1);
  settings.SetEnablePush(true);
  settings.SetMaxConcurrentStreams(3);
  settings.SetInitialWindowSize(4);
  settings.SetMaxFrameSize(50000);
  settings.SetMaxHeaderListSize(6);
  settings.SetAllowTrueBinaryMetadata(true);
  settings.SetPreferredReceiveCryptoMessageSize(77777);
  EXPECT_EQ(settings.header_table_size(), 1u);
  EXPECT_EQ(settings.enable_push(), true);
  EXPECT_EQ(settings.max_concurrent_streams(), 3u);
  EXPECT_EQ(settings.initial_window_size(), 4u);
  EXPECT_EQ(settings.max_frame_size(), 50000u);
  EXPECT_EQ(settings.max_header_list_size(), 6u);
  EXPECT_EQ(settings.allow_true_binary_metadata(), true);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 77777u);
  settings.SetHeaderTableSize(10);
  settings.SetEnablePush(false);
  settings.SetMaxConcurrentStreams(30);
  settings.SetInitialWindowSize(40);
  settings.SetMaxFrameSize(5000000);
  settings.SetMaxHeaderListSize(60);
  settings.SetAllowTrueBinaryMetadata(false);
  settings.SetPreferredReceiveCryptoMessageSize(70000);
  EXPECT_EQ(settings.header_table_size(), 10u);
  EXPECT_EQ(settings.enable_push(), false);
  EXPECT_EQ(settings.max_concurrent_streams(), 30u);
  EXPECT_EQ(settings.initial_window_size(), 40u);
  EXPECT_EQ(settings.max_frame_size(), 5000000u);
  EXPECT_EQ(settings.max_header_list_size(), 60u);
  EXPECT_EQ(settings.allow_true_binary_metadata(), false);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 70000u);
}

TEST(Http2SettingsTest, InitialWindowSizeLimits) {
  Http2Settings settings;
  settings.SetInitialWindowSize(0);
  EXPECT_EQ(settings.initial_window_size(), 0u);
  settings.SetInitialWindowSize(0x7fffffff);
  EXPECT_EQ(settings.initial_window_size(), 0x7fffffffu);
  settings.SetInitialWindowSize(0x80000000);
  EXPECT_EQ(settings.initial_window_size(), 0x7fffffffu);
  settings.SetInitialWindowSize(0xffffffff);
  EXPECT_EQ(settings.initial_window_size(), 0x7fffffffu);
}

TEST(Http2SettingsTest, MaxFrameSizeLimits) {
  Http2Settings settings;
  settings.SetMaxFrameSize(0);
  EXPECT_EQ(settings.max_frame_size(), 16384u);
  settings.SetMaxFrameSize(16384);
  EXPECT_EQ(settings.max_frame_size(), 16384u);
  settings.SetMaxFrameSize(16385);
  EXPECT_EQ(settings.max_frame_size(), 16385u);
  settings.SetMaxFrameSize(16777215);
  EXPECT_EQ(settings.max_frame_size(), 16777215u);
  settings.SetMaxFrameSize(16777216);
  EXPECT_EQ(settings.max_frame_size(), 16777215u);
  settings.SetMaxFrameSize(16777217);
  EXPECT_EQ(settings.max_frame_size(), 16777215u);
  settings.SetMaxFrameSize(0xffffffff);
  EXPECT_EQ(settings.max_frame_size(), 16777215u);
}

TEST(Http2SettingsTest, PreferredReceiveCryptoMessageSize) {
  Http2Settings settings;
  settings.SetPreferredReceiveCryptoMessageSize(0);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 16384u);
  settings.SetPreferredReceiveCryptoMessageSize(16384);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 16384u);
  settings.SetPreferredReceiveCryptoMessageSize(16385);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 16385u);
  settings.SetPreferredReceiveCryptoMessageSize(16777215);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 16777215u);
  settings.SetPreferredReceiveCryptoMessageSize(16777216);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 16777216u);
  settings.SetPreferredReceiveCryptoMessageSize(16777217);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 16777217u);
  settings.SetPreferredReceiveCryptoMessageSize(0x7fffffff);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 0x7fffffffu);
  settings.SetPreferredReceiveCryptoMessageSize(0x80000000);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 0x7fffffffu);
  settings.SetPreferredReceiveCryptoMessageSize(0xffffffff);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 0x7fffffffu);
}

namespace {
using KeyValue = std::pair<uint16_t, uint32_t>;
using KeyValueVec = std::vector<KeyValue>;

KeyValueVec Diff(const Http2Settings& a, const Http2Settings& b,
                 bool is_first_send) {
  KeyValueVec diffs;
  a.Diff(is_first_send, b, [&diffs](uint16_t key, uint32_t value) {
    diffs.emplace_back(key, value);
  });
  return diffs;
}

bool operator==(const KeyValue& a, const Http2SettingsFrame::Setting& b) {
  return a.first == b.id && a.second == b.value;
}

}  // namespace

TEST(Http2SettingsTest, DiffOnFreshlyInitializedSettings) {
  const Http2Settings settings1;
  const Http2Settings settings2;
  EXPECT_THAT(Diff(settings1, settings2, false), ::testing::IsEmpty());
  EXPECT_THAT(Diff(settings1, settings2, true),
              ::testing::UnorderedElementsAre(KeyValue{4, 65535}));
}

TEST(Http2SettingsTest, DiffOnSettingsWithOneValueSet) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetHeaderTableSize(1);
  EXPECT_THAT(Diff(settings1, settings2, false),
              ::testing::UnorderedElementsAre(KeyValue{1, 1}));
  EXPECT_THAT(
      Diff(settings1, settings2, true),
      ::testing::UnorderedElementsAre(KeyValue{1, 1}, KeyValue{4, 65535}));
}

TEST(Http2SettingsTest, DiffOnSettingsWithTwoValuesSet) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetHeaderTableSize(1);
  settings1.SetEnablePush(false);
  EXPECT_THAT(Diff(settings1, settings2, false),
              ::testing::UnorderedElementsAre(KeyValue{1, 1}, KeyValue{2, 0}));
  EXPECT_THAT(Diff(settings1, settings2, true),
              ::testing::UnorderedElementsAre(KeyValue{1, 1}, KeyValue{2, 0},
                                              KeyValue{4, 65535}));
}

TEST(Http2SettingsTest, DiffOnSettingsWithThreeValuesSet) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetHeaderTableSize(1);
  settings1.SetEnablePush(false);
  settings1.SetMaxConcurrentStreams(3);
  EXPECT_THAT(Diff(settings1, settings2, false),
              ::testing::UnorderedElementsAre(KeyValue{1, 1}, KeyValue{2, 0},
                                              KeyValue{3, 3}));
  EXPECT_THAT(
      Diff(settings1, settings2, true),
      ::testing::UnorderedElementsAre(KeyValue{1, 1}, KeyValue{2, 0},
                                      KeyValue{3, 3}, KeyValue{4, 65535}));
}

TEST(Http2SettingsTest, DiffOnSettingsWithFourValuesSet) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetHeaderTableSize(1);
  settings1.SetEnablePush(false);
  settings1.SetMaxConcurrentStreams(3);
  settings1.SetInitialWindowSize(4);
  EXPECT_THAT(Diff(settings1, settings2, false),
              ::testing::UnorderedElementsAre(KeyValue{1, 1}, KeyValue{2, 0},
                                              KeyValue{3, 3}, KeyValue{4, 4}));
  EXPECT_THAT(Diff(settings1, settings2, true),
              ::testing::UnorderedElementsAre(KeyValue{1, 1}, KeyValue{2, 0},
                                              KeyValue{3, 3}, KeyValue{4, 4}));
}

TEST(Http2SettingsTest, DiffOnSettingsWithFiveValuesSet) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetHeaderTableSize(1);
  settings1.SetEnablePush(false);
  settings1.SetMaxConcurrentStreams(3);
  settings1.SetInitialWindowSize(4);
  settings1.SetMaxFrameSize(50000);
  EXPECT_THAT(Diff(settings1, settings2, false),
              ::testing::UnorderedElementsAre(KeyValue{1, 1}, KeyValue{2, 0},
                                              KeyValue{3, 3}, KeyValue{4, 4},
                                              KeyValue{5, 50000}));
  EXPECT_THAT(Diff(settings1, settings2, true),
              ::testing::UnorderedElementsAre(KeyValue{1, 1}, KeyValue{2, 0},
                                              KeyValue{3, 3}, KeyValue{4, 4},
                                              KeyValue{5, 50000}));
}

TEST(Http2SettingsTest, DiffOnSettingsWithSixValuesSet) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetHeaderTableSize(1);
  settings1.SetEnablePush(false);
  settings1.SetMaxConcurrentStreams(3);
  settings1.SetInitialWindowSize(4);
  settings1.SetMaxFrameSize(50000);
  settings1.SetMaxHeaderListSize(6);
  EXPECT_THAT(Diff(settings1, settings2, false),
              ::testing::UnorderedElementsAre(
                  KeyValue{1, 1}, KeyValue{2, 0}, KeyValue{3, 3},
                  KeyValue{4, 4}, KeyValue{5, 50000}, KeyValue{6, 6}));
  EXPECT_THAT(Diff(settings1, settings2, true),
              ::testing::UnorderedElementsAre(
                  KeyValue{1, 1}, KeyValue{2, 0}, KeyValue{3, 3},
                  KeyValue{4, 4}, KeyValue{5, 50000}, KeyValue{6, 6}));
}

TEST(Http2SettingsTest, DiffOnSettingsWithSevenValuesSet) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetHeaderTableSize(1);
  settings1.SetEnablePush(false);
  settings1.SetMaxConcurrentStreams(3);
  settings1.SetInitialWindowSize(4);
  settings1.SetMaxFrameSize(50000);
  settings1.SetMaxHeaderListSize(6);
  settings1.SetAllowTrueBinaryMetadata(true);
  EXPECT_THAT(
      Diff(settings1, settings2, false),
      ::testing::UnorderedElementsAre(
          KeyValue{1, 1}, KeyValue{2, 0}, KeyValue{3, 3}, KeyValue{4, 4},
          KeyValue{5, 50000}, KeyValue{6, 6}, KeyValue{65027, 1}));
  EXPECT_THAT(
      Diff(settings1, settings2, true),
      ::testing::UnorderedElementsAre(
          KeyValue{1, 1}, KeyValue{2, 0}, KeyValue{3, 3}, KeyValue{4, 4},
          KeyValue{5, 50000}, KeyValue{6, 6}, KeyValue{65027, 1}));
}

TEST(Http2SettingsTest, DiffOnSettingsWithEightValuesSet) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetHeaderTableSize(1);
  settings1.SetEnablePush(false);
  settings1.SetMaxConcurrentStreams(3);
  settings1.SetInitialWindowSize(4);
  settings1.SetMaxFrameSize(50000);
  settings1.SetMaxHeaderListSize(6);
  settings1.SetAllowTrueBinaryMetadata(true);
  settings1.SetPreferredReceiveCryptoMessageSize(77777);
  EXPECT_THAT(Diff(settings1, settings2, false),
              ::testing::UnorderedElementsAre(
                  KeyValue{1, 1}, KeyValue{2, 0}, KeyValue{3, 3},
                  KeyValue{4, 4}, KeyValue{5, 50000}, KeyValue{6, 6},
                  KeyValue{65027, 1}, KeyValue{65028, 77777}));
  EXPECT_THAT(Diff(settings1, settings2, true),
              ::testing::UnorderedElementsAre(
                  KeyValue{1, 1}, KeyValue{2, 0}, KeyValue{3, 3},
                  KeyValue{4, 4}, KeyValue{5, 50000}, KeyValue{6, 6},
                  KeyValue{65027, 1}, KeyValue{65028, 77777}));
}

TEST(Http2SettingsTest, ChangingHeaderTableSizeChangesEquality) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetHeaderTableSize(1);
  EXPECT_NE(settings1, settings2);
  settings2.SetHeaderTableSize(1);
  EXPECT_EQ(settings1, settings2);
  settings2.SetHeaderTableSize(2);
  EXPECT_NE(settings1, settings2);
}

TEST(Http2SettingsTest, ChangingEnablePushChangesEquality) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetEnablePush(false);
  EXPECT_NE(settings1, settings2);
  settings2.SetEnablePush(false);
  EXPECT_EQ(settings1, settings2);
  settings2.SetEnablePush(true);
  EXPECT_NE(settings1, settings2);
}

TEST(Http2SettingsTest, ChangingMaxConcurrentStreamsChangesEquality) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetMaxConcurrentStreams(1);
  EXPECT_NE(settings1, settings2);
  settings2.SetMaxConcurrentStreams(1);
  EXPECT_EQ(settings1, settings2);
  settings2.SetMaxConcurrentStreams(2);
  EXPECT_NE(settings1, settings2);
}

TEST(Http2SettingsTest, ChangingInitialWindowSizeChangesEquality) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetInitialWindowSize(1);
  EXPECT_NE(settings1, settings2);
  settings2.SetInitialWindowSize(1);
  EXPECT_EQ(settings1, settings2);
  settings2.SetInitialWindowSize(2);
  EXPECT_NE(settings1, settings2);
}

TEST(Http2SettingsTest, ChangingMaxFrameSizeChangesEquality) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetMaxFrameSize(100000);
  EXPECT_NE(settings1, settings2);
  settings2.SetMaxFrameSize(100000);
  EXPECT_EQ(settings1, settings2);
  settings2.SetMaxFrameSize(200000);
  EXPECT_NE(settings1, settings2);
}

TEST(Http2SettingsTest, ChangingMaxHeaderListSizeChangesEquality) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetMaxHeaderListSize(1);
  EXPECT_NE(settings1, settings2);
  settings2.SetMaxHeaderListSize(1);
  EXPECT_EQ(settings1, settings2);
  settings2.SetMaxHeaderListSize(2);
  EXPECT_NE(settings1, settings2);
}

TEST(Http2SettingsTest, ChangingAllowTrueBinaryMetadataChangesEquality) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetAllowTrueBinaryMetadata(true);
  EXPECT_NE(settings1, settings2);
  settings2.SetAllowTrueBinaryMetadata(true);
  EXPECT_EQ(settings1, settings2);
  settings2.SetAllowTrueBinaryMetadata(false);
  EXPECT_NE(settings1, settings2);
}

TEST(Http2SettingsTest,
     ChangingPreferredReceiveCryptoMessageSizeChangesEquality) {
  Http2Settings settings1;
  Http2Settings settings2;
  settings1.SetPreferredReceiveCryptoMessageSize(100000);
  EXPECT_NE(settings1, settings2);
  settings2.SetPreferredReceiveCryptoMessageSize(100000);
  EXPECT_EQ(settings1, settings2);
  settings2.SetPreferredReceiveCryptoMessageSize(200000);
  EXPECT_NE(settings1, settings2);
}

TEST(Http2SettingsTest, WireIdToNameWorks) {
  EXPECT_EQ(Http2Settings::WireIdToName(1), "HEADER_TABLE_SIZE");
  EXPECT_EQ(Http2Settings::WireIdToName(2), "ENABLE_PUSH");
  EXPECT_EQ(Http2Settings::WireIdToName(3), "MAX_CONCURRENT_STREAMS");
  EXPECT_EQ(Http2Settings::WireIdToName(4), "INITIAL_WINDOW_SIZE");
  EXPECT_EQ(Http2Settings::WireIdToName(5), "MAX_FRAME_SIZE");
  EXPECT_EQ(Http2Settings::WireIdToName(6), "MAX_HEADER_LIST_SIZE");
  EXPECT_EQ(Http2Settings::WireIdToName(65027),
            "GRPC_ALLOW_TRUE_BINARY_METADATA");
  EXPECT_EQ(Http2Settings::WireIdToName(65028),
            "GRPC_PREFERRED_RECEIVE_MESSAGE_SIZE");
  EXPECT_EQ(Http2Settings::WireIdToName(65029), "UNKNOWN (65029)");
}

TEST(Http2SettingsTest, ApplyHeaderTableSizeWorks) {
  Http2Settings settings;
  EXPECT_EQ(settings.Apply(1, 1), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.header_table_size(), 1u);
  EXPECT_EQ(settings.Apply(1, 0x7fffffff), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.header_table_size(), 0x7fffffffu);
}

TEST(Http2SettingsTest, ApplyEnablePushWorks) {
  Http2Settings settings;
  EXPECT_EQ(settings.Apply(2, 0), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.enable_push(), false);
  EXPECT_EQ(settings.Apply(2, 1), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.enable_push(), true);
  EXPECT_EQ(settings.Apply(2, 2), GRPC_HTTP2_PROTOCOL_ERROR);
}

TEST(Http2SettingsTest, ApplyMaxConcurrentStreamsWorks) {
  Http2Settings settings;
  EXPECT_EQ(settings.Apply(3, 1), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.max_concurrent_streams(), 1u);
  EXPECT_EQ(settings.Apply(3, 0x7fffffff), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.max_concurrent_streams(), 0x7fffffffu);
}

TEST(Http2SettingsTest, ApplyInitialWindowSizeWorks) {
  Http2Settings settings;
  EXPECT_EQ(settings.Apply(4, 1), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.initial_window_size(), 1u);
  EXPECT_EQ(settings.Apply(4, 0x7fffffff), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.initial_window_size(), 0x7fffffffu);
}

TEST(Http2SettingsTest, ApplyMaxFrameSizeWorks) {
  Http2Settings settings;
  EXPECT_EQ(settings.Apply(5, 16384), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.max_frame_size(), 16384u);
  EXPECT_EQ(settings.Apply(5, 16777215), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.max_frame_size(), 16777215);
  EXPECT_EQ(settings.Apply(5, 16383), GRPC_HTTP2_PROTOCOL_ERROR);
  EXPECT_EQ(settings.Apply(5, 16777216), GRPC_HTTP2_PROTOCOL_ERROR);
}

TEST(Http2SettingsTest, ApplyMaxHeaderListSizeWorks) {
  Http2Settings settings;
  EXPECT_EQ(settings.Apply(6, 1), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.max_header_list_size(), 1u);
  EXPECT_EQ(settings.Apply(6, 0x7fffffff), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.max_header_list_size(), 16777216);
}

TEST(Http2SettingsTest, ApplyAllowTrueBinaryMetadataWorks) {
  Http2Settings settings;
  EXPECT_EQ(settings.Apply(65027, 0), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.allow_true_binary_metadata(), false);
  EXPECT_EQ(settings.Apply(65027, 1), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.allow_true_binary_metadata(), true);
  EXPECT_EQ(settings.Apply(65027, 2), GRPC_HTTP2_PROTOCOL_ERROR);
}

TEST(Http2SettingsTest, ApplyPreferredReceiveCryptoMessageSizeWorks) {
  Http2Settings settings;
  EXPECT_EQ(settings.Apply(65028, 1), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 16384u);
  EXPECT_EQ(settings.Apply(65028, 0x7fffffff), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 0x7fffffffu);
  EXPECT_EQ(settings.Apply(65028, 0x80000000), GRPC_HTTP2_NO_ERROR);
  EXPECT_EQ(settings.preferred_receive_crypto_message_size(), 0x7fffffffu);
}

namespace {
MATCHER_P(SettingsFrame, settings, "") {
  if (!arg.has_value()) {
    *result_listener << "Expected a settings frame, got nothing";
    return false;
  }
  if (arg->ack) {
    *result_listener << "Expected a settings frame, got an ack";
    return false;
  }
  if (arg->settings.size() != settings.size()) {
    *result_listener << "Expected settings frame with " << settings.size()
                     << " settings, got " << arg->settings.size();
    return false;
  }
  for (size_t i = 0; i < settings.size(); i++) {
    bool found = false;
    for (size_t j = 0; j < arg->settings.size(); j++) {
      if (settings[i] == arg->settings[j]) {
        found = true;
        break;
      }
    }
    if (!found) {
      *result_listener << "Expected settings frame with setting "
                       << settings[i].first << " = " << settings[i].second
                       << ", but it was not found";
      return false;
    }
  }
  return true;
}
}  // namespace

TEST(Http2SettingsManagerTest, ImmediatelyNeedsToSend) {
  Http2SettingsManager settings_manager;
  EXPECT_THAT(settings_manager.MaybeSendUpdate(),
              SettingsFrame(KeyValueVec{{4, 65535}}));
}

TEST(Http2SettingsManagerTest, SendAckWorks) {
  Http2SettingsManager settings_manager;
  settings_manager.mutable_local().SetInitialWindowSize(100000);
  EXPECT_EQ(settings_manager.acked().initial_window_size(), 65535u);
  EXPECT_THAT(settings_manager.MaybeSendUpdate(),
              SettingsFrame(KeyValueVec{{4, 100000}}));
  EXPECT_TRUE(settings_manager.AckLastSend());
  EXPECT_EQ(settings_manager.acked().initial_window_size(), 100000u);
}

TEST(Http2SettingsManagerTest, AckWithoutSendFails) {
  Http2SettingsManager settings_manager;
  EXPECT_FALSE(settings_manager.AckLastSend());
}

TEST(Http2SettingsManagerTest, AckAfterAckFails) {
  Http2SettingsManager settings_manager;
  settings_manager.mutable_local().SetInitialWindowSize(100000);
  EXPECT_THAT(settings_manager.MaybeSendUpdate(),
              SettingsFrame(KeyValueVec{{4, 100000}}));
  EXPECT_TRUE(settings_manager.AckLastSend());
  EXPECT_FALSE(settings_manager.AckLastSend());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
