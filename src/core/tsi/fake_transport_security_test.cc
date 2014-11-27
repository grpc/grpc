/*
 *
 * Copyright 2014, Google Inc.
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

#include "src/core/tsi/fake_transport_security.h"

#include "src/core/tsi/transport_security_test_lib.h"
#include <gtest/gtest.h>
#include "util/random/permute-inl.h"

namespace {

void CheckStringPeerProperty(const tsi_peer& peer, int property_index,
                             const char* expected_name,
                             const char* expected_value) {
  EXPECT_LT(property_index, peer.property_count);
  const tsi_peer_property* property = &peer.properties[property_index];
  EXPECT_EQ(TSI_PEER_PROPERTY_TYPE_STRING, property->type);
  EXPECT_EQ(string(expected_name), string(property->name));
  EXPECT_EQ(string(expected_value),
            string(property->value.string.data, property->value.string.length));
}

class FakeTransportSecurityTest : public tsi::test::TransportSecurityTest {
 protected:
  void SetupHandshakers() override {
    client_handshaker_.reset(tsi_create_fake_handshaker(1));
    server_handshaker_.reset(tsi_create_fake_handshaker(0));
  }

  void CheckPeer(tsi_handshaker* handshaker) {
    tsi_peer peer;
    EXPECT_EQ(TSI_OK, tsi_handshaker_extract_peer(handshaker, &peer));
    EXPECT_EQ(1, peer.property_count);
    CheckStringPeerProperty(peer, 0, TSI_CERTIFICATE_TYPE_PEER_PROPERTY,
                            TSI_FAKE_CERTIFICATE_TYPE);
    tsi_peer_destruct(&peer);
  }

  void CheckHandshakeResults() override {
    CheckPeer(client_handshaker_.get());
    CheckPeer(server_handshaker_.get());
  }

  const tsi::test::TestConfig* config() {
    return &config_;
  }

  tsi::test::TestConfig config_;
};

TEST_F(FakeTransportSecurityTest, Handshake) {
  PerformHandshake();
}

TEST_F(FakeTransportSecurityTest, HandshakeSmallBuffer) {
  config_.handshake_buffer_size = 3;
  PerformHandshake();
}
TEST_F(FakeTransportSecurityTest, PingPong) {
  PingPong();
}

TEST_F(FakeTransportSecurityTest, RoundTrip) {
  config_.client_message = big_message_;
  config_.server_message = small_message_;
  DoRoundTrip();
}

TEST_F(FakeTransportSecurityTest, RoundTripSmallMessageBuffer) {
  config_.message_buffer_allocated_size = 42;
  config_.client_message = big_message_;
  config_.server_message = small_message_;
  DoRoundTrip();
}

TEST_F(FakeTransportSecurityTest, RoundTripSmallProtectedBufferSize) {
  config_.protected_buffer_size = 37;
  config_.client_message = big_message_;
  config_.server_message = small_message_;
  DoRoundTrip();
}

TEST_F(FakeTransportSecurityTest, RoundTripSmallReadBufferSize) {
  config_.read_buffer_allocated_size = 41;
  config_.client_message = big_message_;
  config_.server_message = small_message_;
  DoRoundTrip();
}

TEST_F(FakeTransportSecurityTest, RoundTripSmallClientFrames) {
  config_.set_client_max_output_protected_frame_size(39);
  config_.client_message = big_message_;
  config_.server_message = small_message_;
  DoRoundTrip();
}

TEST_F(FakeTransportSecurityTest, RoundTripSmallServerFrames) {
  config_.set_server_max_output_protected_frame_size(43);
  config_.client_message = small_message_;
  config_.server_message = big_message_;
  DoRoundTrip();
}

TEST_F(FakeTransportSecurityTest, RoundTripOddBufferSizes) {
  int odd_sizes[] = {33, 67, 135, 271, 523};
  RandomPermutation<int> permute(odd_sizes, arraysize(odd_sizes),
                                 random_.get());
  permute.Permute();
  LOG(ERROR) << odd_sizes[0] << "\t" << odd_sizes[1] << "\t" << odd_sizes[2]
             << "\t" << odd_sizes[3] << "\t" << odd_sizes[4];
  config_.message_buffer_allocated_size = odd_sizes[0];
  config_.protected_buffer_size = odd_sizes[1];
  config_.read_buffer_allocated_size = odd_sizes[2];
  config_.set_client_max_output_protected_frame_size(odd_sizes[3]);
  config_.set_server_max_output_protected_frame_size(odd_sizes[4]);
  config_.client_message = big_message_;
  config_.server_message = small_message_;
  DoRoundTrip();
}

}  // namespace
