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

#include <memory>

#include "base/commandlineflags.h"
#include "file/base/helpers.h"
#include "file/base/options.pb.h"
#include "file/base/path.h"
#include "src/core/tsi/transport_security_test_lib.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "util/random/permute-inl.h"

namespace {

const char kTestCredsDir[] =
    "/internal/tsi/test_creds/";

enum AlpnMode {
  NO_ALPN,
  ALPN_CLIENT_NO_SERVER,
  ALPN_SERVER_NO_CLIENT,
  ALPN_CLIENT_SERVER_OK,
  ALPN_CLIENT_SERVER_MISMATCH
};

class SslTestConfig : public tsi::test::TestConfig {
 public:
  SslTestConfig()
      : do_client_authentication(false),
        subject_name_indication(nullptr),
        use_bad_client_cert(false),
        use_bad_server_cert(false),
        alpn_mode(NO_ALPN) {}
  bool do_client_authentication;
  const char* subject_name_indication;
  bool use_bad_client_cert;
  bool use_bad_server_cert;
  AlpnMode alpn_mode;
};

struct TsiSslHandshakerFactoryDeleter {
  inline void operator()(tsi_ssl_handshaker_factory* ptr) {
    tsi_ssl_handshaker_factory_destroy(ptr);
  }
};
typedef std::unique_ptr<tsi_ssl_handshaker_factory,
                        TsiSslHandshakerFactoryDeleter>
    TsiSslHandshakerFactoryUniquePtr;

class SslTransportSecurityTest : public tsi::test::TransportSecurityTest {
 protected:
  void CheckSubjectAltName(const tsi_peer_property& property,
                           const string& expected_subject_alt_name) {
    EXPECT_EQ(property.type, TSI_PEER_PROPERTY_TYPE_STRING);
    EXPECT_EQ(property.name, nullptr);
    EXPECT_EQ(
        string(property.value.string.data, property.value.string.length),
        expected_subject_alt_name);
  }

  const tsi_peer_property* CheckBasicAuthenticatedPeerAndGetCommonName(
      const tsi_peer* peer) {
    const tsi_peer_property* property =
        tsi_peer_get_property_by_name(peer, TSI_CERTIFICATE_TYPE_PEER_PROPERTY);
    EXPECT_NE(property, nullptr);
    EXPECT_EQ(property->type, TSI_PEER_PROPERTY_TYPE_STRING);
    EXPECT_EQ(
        string(property->value.string.data, property->value.string.length),
        string(TSI_X509_CERTIFICATE_TYPE));
    property = tsi_peer_get_property_by_name(
        peer, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY);
    EXPECT_EQ(property->type, TSI_PEER_PROPERTY_TYPE_STRING);
    return property;
  }

  void CheckServer0Peer(tsi_peer* peer) {
    const tsi_peer_property* property =
        CheckBasicAuthenticatedPeerAndGetCommonName(peer);
    EXPECT_EQ(
        string(property->value.string.data, property->value.string.length),
        string("*.test.google.com.au"));
    property = tsi_peer_get_property_by_name(
        peer, TSI_X509_SUBJECT_ALTERNATIVE_NAMES_PEER_PROPERTY);
    EXPECT_EQ(property->type, TSI_PEER_PROPERTY_TYPE_LIST);
    EXPECT_EQ(property->value.list.child_count, 0);
    EXPECT_EQ(1, tsi_ssl_peer_matches_name(peer, "foo.test.google.com.au"));
    EXPECT_EQ(1, tsi_ssl_peer_matches_name(peer, "bar.test.google.com.au"));
    EXPECT_EQ(0, tsi_ssl_peer_matches_name(peer, "bar.test.google.blah"));
    EXPECT_EQ(0, tsi_ssl_peer_matches_name(peer, "foo.bar.test.google.com.au"));
    EXPECT_EQ(0, tsi_ssl_peer_matches_name(peer, "test.google.com.au"));
    tsi_peer_destruct(peer);
  }

  void CheckServer1Peer(tsi_peer* peer) {
    const tsi_peer_property* property =
        CheckBasicAuthenticatedPeerAndGetCommonName(peer);
    EXPECT_EQ(
        string(property->value.string.data, property->value.string.length),
        string("*.test.google.com"));
    property = tsi_peer_get_property_by_name(
        peer, TSI_X509_SUBJECT_ALTERNATIVE_NAMES_PEER_PROPERTY);
    EXPECT_EQ(property->type, TSI_PEER_PROPERTY_TYPE_LIST);
    EXPECT_EQ(property->value.list.child_count, 3);
    CheckSubjectAltName(property->value.list.children[0], "*.test.google.fr");
    CheckSubjectAltName(property->value.list.children[1],
                        "waterzooi.test.google.be");
    CheckSubjectAltName(property->value.list.children[2], "*.test.youtube.com");
    EXPECT_EQ(1, tsi_ssl_peer_matches_name(peer, "foo.test.google.com"));
    EXPECT_EQ(1, tsi_ssl_peer_matches_name(peer, "bar.test.google.fr"));
    EXPECT_EQ(1, tsi_ssl_peer_matches_name(peer, "waterzooi.test.google.be"));
    EXPECT_EQ(1, tsi_ssl_peer_matches_name(peer, "foo.test.youtube.com"));
    EXPECT_EQ(0, tsi_ssl_peer_matches_name(peer, "bar.foo.test.google.com"));
    EXPECT_EQ(0, tsi_ssl_peer_matches_name(peer, "test.google.fr"));
    EXPECT_EQ(0, tsi_ssl_peer_matches_name(peer, "tartines.test.google.be"));
    EXPECT_EQ(0, tsi_ssl_peer_matches_name(peer, "tartines.youtube.com"));
    tsi_peer_destruct(peer);
  }

  void CheckClientPeer(tsi_peer* peer, bool is_authenticated) {
    if (!is_authenticated) {
      EXPECT_EQ(peer->property_count,
                config_.alpn_mode == ALPN_CLIENT_SERVER_OK ? 1 : 0);
    } else {
      const tsi_peer_property* property =
          CheckBasicAuthenticatedPeerAndGetCommonName(peer);
      EXPECT_EQ(
          string(property->value.string.data, property->value.string.length),
          string("testclient"));
    }
    tsi_peer_destruct(peer);
  }

  void SetupHandshakers() override {
    tsi_ssl_handshaker_factory* client_handshaker_factory;
    const unsigned char* client_cert = NULL;
    unsigned int client_cert_size = 0;
    const unsigned char* client_key = NULL;
    unsigned int client_key_size = 0;
    if (config_.do_client_authentication) {
      if (config_.use_bad_client_cert) {
        client_cert =
            reinterpret_cast<const unsigned char*>(badclient_cert_.data());
        client_cert_size = badclient_cert_.size();
        client_key =
            reinterpret_cast<const unsigned char*>(badclient_key_.data());
        client_key_size = badclient_key_.size();
      } else {
        client_cert =
            reinterpret_cast<const unsigned char*>(client_cert_.data());
        client_cert_size = client_cert_.size();
        client_key = reinterpret_cast<const unsigned char*>(client_key_.data());
        client_key_size = client_key_.size();
      }
    }
    const unsigned char** client_alpn_protocols(nullptr);
    const unsigned char* client_alpn_protocols_lengths(nullptr);
    uint16_t num_client_alpn_protocols = 0;
    if (config_.alpn_mode == ALPN_CLIENT_NO_SERVER ||
        config_.alpn_mode == ALPN_CLIENT_SERVER_OK ||
        config_.alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
      client_alpn_protocols =
          reinterpret_cast<const unsigned char**>(&client_alpn_protocols_[0]);
      client_alpn_protocols_lengths = &client_alpn_protocols_lengths_[0];
      num_client_alpn_protocols = client_alpn_protocols_.size();
    }

    EXPECT_EQ(tsi_create_ssl_client_handshaker_factory(
                  client_key, client_key_size, client_cert, client_cert_size,
                  reinterpret_cast<const unsigned char*>(root_certs_.data()),
                  root_certs_.size(), NULL, client_alpn_protocols,
                  client_alpn_protocols_lengths, num_client_alpn_protocols,
                  &client_handshaker_factory),
              TSI_OK);
    client_handshaker_factory_.reset(client_handshaker_factory);

    const unsigned char** server_alpn_protocols(nullptr);
    const unsigned char* server_alpn_protocols_lengths(nullptr);
    uint16_t num_server_alpn_protocols = 0;
    if (config_.alpn_mode == ALPN_SERVER_NO_CLIENT ||
        config_.alpn_mode == ALPN_CLIENT_SERVER_OK ||
        config_.alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
      server_alpn_protocols =
          reinterpret_cast<const unsigned char**>(&server_alpn_protocols_[0]);
      server_alpn_protocols_lengths = &server_alpn_protocols_lengths_[0];
      num_server_alpn_protocols = server_alpn_protocols_.size();
      if (config_.alpn_mode == ALPN_CLIENT_SERVER_MISMATCH) {
        // Remove the last element that is common.
        num_server_alpn_protocols--;
      }
    }
    tsi_ssl_handshaker_factory* server_handshaker_factory;
    EXPECT_EQ(
        tsi_create_ssl_server_handshaker_factory(
            config_.use_bad_server_cert ? &badserver_keys_[0]
                                        : &server_keys_[0],
            config_.use_bad_server_cert ? &badserver_keys_sizes_[0]
                                        : &server_keys_sizes_[0],
            config_.use_bad_server_cert ? &badserver_certs_[0]
                                        : &server_certs_[0],
            config_.use_bad_server_cert ? &badserver_certs_sizes_[0]
                                        : &server_certs_sizes_[0],
            config_.use_bad_server_cert ? badserver_keys_.size()
                                        : server_keys_.size(),
            config_.do_client_authentication
                ? reinterpret_cast<const unsigned char*>(root_certs_.data())
                : NULL,
            config_.do_client_authentication ? root_certs_.size() : 0, NULL,
            server_alpn_protocols, server_alpn_protocols_lengths,
            num_server_alpn_protocols, &server_handshaker_factory),
        TSI_OK);
    server_handshaker_factory_.reset(server_handshaker_factory);

    tsi_handshaker* client_handshaker;
    EXPECT_EQ(tsi_ssl_handshaker_factory_create_handshaker(
                  client_handshaker_factory, config_.subject_name_indication,
                  &client_handshaker),
              TSI_OK);
    client_handshaker_.reset(client_handshaker);

    tsi_handshaker* server_handshaker;
    EXPECT_EQ(tsi_ssl_handshaker_factory_create_handshaker(
                  server_handshaker_factory, NULL, &server_handshaker),
              TSI_OK);
    server_handshaker_.reset(server_handshaker);
  }

  void CheckAlpn(const tsi_peer* peer) {
    const tsi_peer_property* alpn_property =
        tsi_peer_get_property_by_name(peer, TSI_SSL_ALPN_SELECTED_PROTOCOL);
    if (config_.alpn_mode != ALPN_CLIENT_SERVER_OK) {
      EXPECT_EQ(nullptr, alpn_property);
    } else {
      EXPECT_NE(nullptr, alpn_property);
      EXPECT_EQ(TSI_PEER_PROPERTY_TYPE_STRING, alpn_property->type);
      string expected_match("baz");
      EXPECT_EQ(expected_match, string(alpn_property->value.string.data,
                                       alpn_property->value.string.length));
    }
  }

  void CheckHandshakeResults() override {
    tsi_peer peer;

    bool expect_success =
        !(config_.use_bad_server_cert ||
          (config_.use_bad_client_cert && config_.do_client_authentication));
    tsi_result result = tsi_handshaker_get_result(client_handshaker_.get());
    EXPECT_NE(result, TSI_HANDSHAKE_IN_PROGRESS);
    if (expect_success) {
      EXPECT_EQ(result, TSI_OK);
      EXPECT_EQ(tsi_handshaker_extract_peer(client_handshaker_.get(), &peer),
                TSI_OK);
      CheckAlpn(&peer);
      // TODO(jboeuf): This is a bit fragile. Maybe revisit.
      if (config_.subject_name_indication != nullptr) {
        CheckServer1Peer(&peer);
      } else {
        CheckServer0Peer(&peer);
      }
    } else {
      EXPECT_NE(result, TSI_OK);
      EXPECT_NE(tsi_handshaker_extract_peer(client_handshaker_.get(), &peer),
                TSI_OK);
    }

    result = tsi_handshaker_get_result(server_handshaker_.get());
    EXPECT_NE(result, TSI_HANDSHAKE_IN_PROGRESS);
    if (expect_success) {
      EXPECT_EQ(result, TSI_OK);
      EXPECT_EQ(tsi_handshaker_extract_peer(server_handshaker_.get(), &peer),
                TSI_OK);
      CheckAlpn(&peer);
      CheckClientPeer(&peer, config_.do_client_authentication);
    } else {
      EXPECT_NE(result, TSI_OK);
      EXPECT_NE(tsi_handshaker_extract_peer(server_handshaker_.get(), &peer),
                TSI_OK);
    }
  }

  const tsi::test::TestConfig* config() override {
    return &config_;
  }

  SslTransportSecurityTest()
      : client_alpn_protocols_({"foo", "toto", "baz"}),
        server_alpn_protocols_({"boooo", "far", "baz"}),
        client_alpn_protocols_lengths_({3, 4, 3}),
        server_alpn_protocols_lengths_({5, 3, 3}) {
    CHECK_OK(file::GetContents(
        file::JoinPath(FLAGS_test_srcdir, kTestCredsDir, "badserver.key"),
        &badserver_key_, file::Options()));
    CHECK_OK(file::GetContents(
        file::JoinPath(FLAGS_test_srcdir, kTestCredsDir, "badserver.pem"),
        &badserver_cert_, file::Options()));
    CHECK_OK(file::GetContents(
        file::JoinPath(FLAGS_test_srcdir, kTestCredsDir, "badclient.key"),
        &badclient_key_, file::Options()));
    CHECK_OK(file::GetContents(
        file::JoinPath(FLAGS_test_srcdir, kTestCredsDir, "badclient.pem"),
        &badclient_cert_, file::Options()));
    CHECK_OK(file::GetContents(
        file::JoinPath(FLAGS_test_srcdir, kTestCredsDir, "server0.key"),
        &server0_key_, file::Options()));
    CHECK_OK(file::GetContents(
        file::JoinPath(FLAGS_test_srcdir, kTestCredsDir, "server0.pem"),
        &server0_cert_, file::Options()));
    CHECK_OK(file::GetContents(
        file::JoinPath(FLAGS_test_srcdir, kTestCredsDir, "server1.key"),
        &server1_key_, file::Options()));
    CHECK_OK(file::GetContents(
        file::JoinPath(FLAGS_test_srcdir, kTestCredsDir, "server1.pem"),
        &server1_cert_, file::Options()));
    CHECK_OK(file::GetContents(
        file::JoinPath(FLAGS_test_srcdir, kTestCredsDir, "client.key"),
        &client_key_, file::Options()));
    CHECK_OK(file::GetContents(
        file::JoinPath(FLAGS_test_srcdir, kTestCredsDir, "client.pem"),
        &client_cert_, file::Options()));
    CHECK_OK(file::GetContents(
        file::JoinPath(FLAGS_test_srcdir, kTestCredsDir, "ca.pem"),
        &root_certs_, file::Options()));
    badserver_keys_.push_back(
        reinterpret_cast<const unsigned char*>(badserver_key_.data()));
    badserver_certs_.push_back(
        reinterpret_cast<const unsigned char*>(badserver_cert_.data()));
    server_keys_.push_back(
        reinterpret_cast<const unsigned char*>(server0_key_.data()));
    server_keys_.push_back(
        reinterpret_cast<const unsigned char*>(server1_key_.data()));
    server_certs_.push_back(
        reinterpret_cast<const unsigned char*>(server0_cert_.data()));
    server_certs_.push_back(
        reinterpret_cast<const unsigned char*>(server1_cert_.data()));
    badserver_keys_sizes_.push_back(badserver_key_.size());
    badserver_certs_sizes_.push_back(badserver_cert_.size());
    server_keys_sizes_.push_back(server0_key_.size());
    server_keys_sizes_.push_back(server1_key_.size());
    server_certs_sizes_.push_back(server0_cert_.size());
    server_certs_sizes_.push_back(server1_cert_.size());
  }

  string badserver_key_;
  string badserver_cert_;
  string badclient_key_;
  string badclient_cert_;
  string server0_key_;
  string server0_cert_;
  string server1_key_;
  string server1_cert_;
  string client_key_;
  string client_cert_;
  string root_certs_;
  std::vector<const unsigned char*> badserver_keys_;
  std::vector<const unsigned char*> badserver_certs_;
  std::vector<const unsigned char*> server_keys_;
  std::vector<const unsigned char*> server_certs_;
  std::vector<unsigned int> badserver_keys_sizes_;
  std::vector<unsigned int> badserver_certs_sizes_;
  std::vector<unsigned int> server_keys_sizes_;
  std::vector<unsigned int> server_certs_sizes_;
  TsiSslHandshakerFactoryUniquePtr client_handshaker_factory_;
  TsiSslHandshakerFactoryUniquePtr server_handshaker_factory_;
  std::vector<const char*> client_alpn_protocols_;
  std::vector<const char*> server_alpn_protocols_;
  std::vector<unsigned char> client_alpn_protocols_lengths_;
  std::vector<unsigned char> server_alpn_protocols_lengths_;
  string matched_alpn_;
  SslTestConfig config_;
};


TEST_F(SslTransportSecurityTest, LoadInvalidRoots) {
  tsi_ssl_handshaker_factory* client_handshaker_factory;
  string invalid_roots("Invalid roots!");
  EXPECT_EQ(
      TSI_INVALID_ARGUMENT,
      tsi_create_ssl_client_handshaker_factory(
          NULL, 0, NULL, 0,
          reinterpret_cast<const unsigned char*>(invalid_roots.data()),
          invalid_roots.size(), NULL, NULL, 0, 0, &client_handshaker_factory));
}

TEST_F(SslTransportSecurityTest, Handshake) {
  PerformHandshake();
}

TEST_F(SslTransportSecurityTest, HandshakeClientAuthentication) {
  config_.do_client_authentication = true;
  PerformHandshake();
}

TEST_F(SslTransportSecurityTest, HandshakeSmallBuffer) {
  config_.handshake_buffer_size = 128;
  PerformHandshake();
}

TEST_F(SslTransportSecurityTest, HandshakeSNIExactDomain) {
  // server1 cert contains waterzooi.test.google.be in SAN.
  config_.subject_name_indication = "waterzooi.test.google.be";
  PerformHandshake();
}

TEST_F(SslTransportSecurityTest, HandshakeSNIWildstarDomain) {
  // server1 cert contains *.test.google.fr in SAN.
  config_.subject_name_indication = "juju.test.google.fr";
  PerformHandshake();
}

TEST_F(SslTransportSecurityTest, BadServerCertFailure) {
  config_.use_bad_server_cert = true;
  PerformHandshake();
}

TEST_F(SslTransportSecurityTest, BadClientCertFailure) {
  config_.use_bad_client_cert = true;
  config_.do_client_authentication = true;
  PerformHandshake();
}

TEST_F(SslTransportSecurityTest, AlpnClientNoServer) {
  config_.alpn_mode = ALPN_CLIENT_NO_SERVER;
  PerformHandshake();
}

TEST_F(SslTransportSecurityTest, AlpnServerNoClient) {
  config_.alpn_mode = ALPN_SERVER_NO_CLIENT;
  PerformHandshake();
}

TEST_F(SslTransportSecurityTest, AlpnClientServeMismatch) {
  config_.alpn_mode = ALPN_CLIENT_SERVER_MISMATCH;
  PerformHandshake();
}

TEST_F(SslTransportSecurityTest, AlpnClientServerOk) {
  config_.alpn_mode = ALPN_CLIENT_SERVER_OK;
  PerformHandshake();
}

TEST_F(SslTransportSecurityTest, PingPong) {
  PingPong();
}

TEST_F(SslTransportSecurityTest, RoundTrip) {
  config_.client_message = big_message_;
  config_.server_message = small_message_;
  DoRoundTrip();
}

TEST_F(SslTransportSecurityTest, RoundTripSmallMessageBuffer) {
  config_.message_buffer_allocated_size = 42;
  config_.client_message = big_message_;
  config_.server_message = small_message_;
  DoRoundTrip();
}

TEST_F(SslTransportSecurityTest, RoundTripSmallProtectedBufferSize) {
  config_.protected_buffer_size = 37;
  config_.client_message = big_message_;
  config_.server_message = small_message_;
  DoRoundTrip();
}

TEST_F(SslTransportSecurityTest, RoundTripSmallReadBufferSize) {
  config_.read_buffer_allocated_size = 41;
  config_.client_message = big_message_;
  config_.server_message = small_message_;
  DoRoundTrip();
}

TEST_F(SslTransportSecurityTest, RoundTripSmallClientFrames) {
  config_.set_client_max_output_protected_frame_size(39);
  config_.client_message = big_message_;
  config_.server_message = small_message_;
  DoRoundTrip();
}

TEST_F(SslTransportSecurityTest, RoundTripSmallServerFrames) {
  config_.set_server_max_output_protected_frame_size(43);
  config_.client_message = small_message_;
  config_.server_message = big_message_;
  DoRoundTrip();
}

TEST_F(SslTransportSecurityTest, RoundTripOddBufferSizes) {
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
