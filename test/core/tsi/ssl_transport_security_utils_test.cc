//
// Copyright 2022 gRPC authors.
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

#include "src/core/tsi/ssl_transport_security_utils.h"

#include <array>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include "src/core/tsi/transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

using ::testing::ContainerEq;
using ::testing::NotNull;
using ::testing::TestWithParam;
using ::testing::ValuesIn;

constexpr std::size_t kMaxPlaintextBytesPerTlsRecord = 16384;
constexpr std::size_t kTlsRecordOverhead = 100;
constexpr std::array<std::size_t, 4> kTestPlainTextSizeArray = {
    1, 1000, kMaxPlaintextBytesPerTlsRecord,
    kMaxPlaintextBytesPerTlsRecord + 1000};

struct FrameProtectorUtilTestData {
  std::size_t plaintext_size;
  std::size_t expected_encrypted_bytes_size;
};

// Generates the testing data |FrameProtectorUtilTestData|.
std::vector<FrameProtectorUtilTestData> GenerateTestData() {
  std::vector<FrameProtectorUtilTestData> data;
  for (std::size_t plaintext_size : kTestPlainTextSizeArray) {
    std::size_t expected_size = plaintext_size + kTlsRecordOverhead;
    if (plaintext_size > kMaxPlaintextBytesPerTlsRecord) {
      expected_size = kMaxPlaintextBytesPerTlsRecord + kTlsRecordOverhead;
    }
    data.push_back({plaintext_size, expected_size});
  }
  return data;
}

// TODO(gtcooke94) - Tests current failing with OpenSSL 1.1.1 and 3.0. Fix and
// re-enable.
#ifdef OPENSSL_IS_BORINGSSL
class FlowTest : public TestWithParam<FrameProtectorUtilTestData> {
 protected:
  static void SetUpTestSuite() {
#if OPENSSL_VERSION_NUMBER >= 0x10100000
    OPENSSL_init_ssl(/*opts=*/0, /*settings=*/nullptr);
#else
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
#endif
  }

  // Used for debugging.
  static int VerifySucceed(X509_STORE_CTX* /*store_ctx*/, void* /*arg*/) {
    return 1;
  }

  // Drives two SSL objects to finish a complete handshake with the hard-coded
  // credentials and outputs those two SSL objects to `out_client` and
  // `out_server` respectively.
  static absl::Status DoHandshake(SSL** out_client, SSL** out_server) {
    if (out_client == nullptr || out_server == nullptr) {
      return absl::InvalidArgumentError(
          "Client and server SSL object must not be null.");
    }
    std::string cert_pem =
        "-----BEGIN CERTIFICATE-----\n"
        "MIICZzCCAdCgAwIBAgIIN18/ctj3wpAwDQYJKoZIhvcNAQELBQAwKjEXMBUGA1UE\n"
        "ChMOR29vZ2xlIFRFU1RJTkcxDzANBgNVBAMTBnRlc3RDQTAeFw0xNTAxMDEwMDAw\n"
        "MDBaFw0yNTAxMDEwMDAwMDBaMC8xFzAVBgNVBAoTDkdvb2dsZSBURVNUSU5HMRQw\n"
        "EgYDVQQDDAt0ZXN0X2NlcnRfMTCBnzANBgkqhkiG9w0BAQEFAAOBjQAwgYkCgYEA\n"
        "20oOyI+fNCCeHJ3DNjGooPPP43Q6emhVvuWD8ppta582Rgxq/4j1bl9cPHdoCdyy\n"
        "HsWFVUZzscj2qhClmlBAMEA595OU2NX2d81nSih5dwZWLMRQkEIzyxUR7Vee3eyo\n"
        "nQD4HSamaevMSv79WTUBCozEGITqWnjYA152KUbA/IsCAwEAAaOBkDCBjTAOBgNV\n"
        "HQ8BAf8EBAMCBaAwHQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMAwGA1Ud\n"
        "EwEB/wQCMAAwGQYDVR0OBBIEECnFWP/UkDrV+SoXra58k64wGwYDVR0jBBQwEoAQ\n"
        "p7JSbajiTZaIRUDSV1C81jAWBgNVHREEDzANggt0ZXN0X2NlcnRfMTANBgkqhkiG\n"
        "9w0BAQsFAAOBgQCpJJssfN62T3G5z+5SBB+9KCzXnGxcTHtaTJkb04KLe+19EwhV\n"
        "yRY4lZadKHjcNS6GCBogd069wNFUVYOU9VI7uUiEPdcTO+VRV5MYW0wjSi1zlkBZ\n"
        "e8OAfYVeGUMfvThFpJ41f8vZ6GHgg95Lwv+Zh89SL8g1J3RWll9YVG8HWw==\n"
        "-----END CERTIFICATE-----\n";
    std::string key_pem =
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "MIICXQIBAAKBgQDbSg7Ij580IJ4cncM2Maig88/jdDp6aFW+5YPymm1rnzZGDGr/\n"
        "iPVuX1w8d2gJ3LIexYVVRnOxyPaqEKWaUEAwQDn3k5TY1fZ3zWdKKHl3BlYsxFCQ\n"
        "QjPLFRHtV57d7KidAPgdJqZp68xK/v1ZNQEKjMQYhOpaeNgDXnYpRsD8iwIDAQAB\n"
        "AoGAbq4kZApJeo/z/dGK0/GggQxOIylo0puSm7VQMcTL8YP8asKdxrgj2D99WG+U\n"
        "LVYc+PcM4wuaHWOnTBL24roaitCNhrpIsJfWDkexzHXMj622SYlUcCuwsfjYOEyw\n"
        "ntoNAnh0o4S+beYAfzT5VHCh4is9G9u+mwKYiGpJXROrYUECQQD4eq4nuGq3mfYJ\n"
        "B0+md30paDVVCyBsuZTAtnu3MbRjMXy5LLE+vhno5nocvVSTOv3QC7Wk6yAa8/bG\n"
        "iPT/MWixAkEA4e0zqPGo8tSimVv/1ei8Chyb+YqdSx+Oj5eZpa6X/KB/C1uS1tm6\n"
        "DTgHW2GUhV4ypqdGH+t8quprJUtFuzqH+wJBAMRiicSg789eouMt4RjrdYPFdela\n"
        "Gu1zm4rYb10xrqV7Vl0wYoH5U5cMmdSfGvomdLX6mzzWDJDg4ti1JBWRonECQQCD\n"
        "Umtq0j1QGQUCe5Vz8zoJ7qNDI61WU1t8X7Rxt9CkiW4PXgU2WYxpzp2IImpAM4bh\n"
        "k+2Q9EKc3nG1VdGMiPMtAkARkQF+pL8SBrUoh8G8glCam0brh3tW/cdW8L4UGTNF\n"
        "2ZKC/LFH6DQBjYs3UXjvMGJxz4k9LysyY6o2Nf1JG6/L\n"
        "-----END RSA PRIVATE KEY-----\n";

    // Create the context objects.
    SSL_CTX* client_ctx = nullptr;
    SSL_CTX* server_ctx = nullptr;
#if OPENSSL_VERSION_NUMBER >= 0x10100000
    client_ctx = SSL_CTX_new(TLS_method());
    server_ctx = SSL_CTX_new(TLS_method());
#else
    client_ctx = SSL_CTX_new(TLSv1_2_method());
    server_ctx = SSL_CTX_new(TLSv1_2_method());
#endif

    BIO* client_cert_bio(BIO_new_mem_buf(cert_pem.c_str(), cert_pem.size()));
    X509* client_cert = PEM_read_bio_X509(client_cert_bio, /*x=*/nullptr,
                                          /*cb=*/nullptr, /*u=*/nullptr);
    BIO* client_key_bio(BIO_new_mem_buf(key_pem.c_str(), key_pem.size()));
    EVP_PKEY* client_key =
        PEM_read_bio_PrivateKey(client_key_bio, /*x=*/nullptr,
                                /*cb=*/nullptr, /*u=*/nullptr);

    BIO* server_cert_bio(BIO_new_mem_buf(cert_pem.c_str(), cert_pem.size()));
    X509* server_cert = PEM_read_bio_X509(server_cert_bio, /*x=*/nullptr,
                                          /*cb=*/nullptr, /*u=*/nullptr);
    BIO* server_key_bio(BIO_new_mem_buf(key_pem.c_str(), key_pem.size()));
    EVP_PKEY* server_key =
        PEM_read_bio_PrivateKey(server_key_bio, /*x=*/nullptr,
                                /*cb=*/nullptr, /*u=*/nullptr);

    // Set both client and server certificate and private key.
    SSL_CTX_use_certificate(client_ctx, client_cert);
    SSL_CTX_use_PrivateKey(client_ctx, client_key);
    SSL_CTX_use_certificate(server_ctx, server_cert);
    SSL_CTX_use_PrivateKey(server_ctx, server_key);

    EVP_PKEY_free(client_key);
    BIO_free(client_key_bio);
    X509_free(client_cert);
    BIO_free(client_cert_bio);

    EVP_PKEY_free(server_key);
    BIO_free(server_key_bio);
    X509_free(server_cert);
    BIO_free(server_cert_bio);

    // Configure both client and server to request (and accept any)
    // certificate but fail if none is sent.
    SSL_CTX_set_verify(client_ctx,
                       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       /*callback=*/nullptr);
    SSL_CTX_set_cert_verify_callback(client_ctx, VerifySucceed,
                                     /*arg=*/nullptr);
    SSL_CTX_set_verify(server_ctx,
                       SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                       /*callback=*/nullptr);
    SSL_CTX_set_cert_verify_callback(server_ctx, VerifySucceed,
                                     /*arg=*/nullptr);

    // Turns off the session caching.
    SSL_CTX_set_session_cache_mode(client_ctx, SSL_SESS_CACHE_OFF);
    SSL_CTX_set_session_cache_mode(server_ctx, SSL_SESS_CACHE_OFF);

#if OPENSSL_VERSION_NUMBER >= 0x10100000
#if defined(TLS1_3_VERSION)
    // Set both the min and max TLS version to 1.3
    SSL_CTX_set_min_proto_version(client_ctx, TLS1_3_VERSION);
    SSL_CTX_set_min_proto_version(server_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(client_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(server_ctx, TLS1_3_VERSION);
#else
    SSL_CTX_set_min_proto_version(client_ctx, TLS1_2_VERSION);
    SSL_CTX_set_min_proto_version(server_ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(client_ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(server_ctx, TLS1_2_VERSION);
#endif
#endif

    // Create client and server connection objects and configure their BIOs.
    SSL* client(SSL_new(client_ctx));
    SSL* server(SSL_new(server_ctx));

    SSL_CTX_free(client_ctx);
    SSL_CTX_free(server_ctx);

    // Turns off issuance of session tickets by servers.
    SSL_set_options(client, SSL_OP_NO_TICKET);
    SSL_set_options(server, SSL_OP_NO_TICKET);

    SSL_set_connect_state(client);
    SSL_set_accept_state(server);
    BIO* bio1;
    BIO* bio2;
    BIO_new_bio_pair(&bio1, /*writebuf1=*/0, &bio2, /*writebuf2=*/0);
    SSL_set_bio(client, bio1, bio1);
    SSL_set_bio(server, bio2, bio2);

    // Drive both the client and server handshake operations to completion.
    while (true) {
      int client_ret = SSL_do_handshake(client);
      int client_err = SSL_get_error(client, client_ret);
      if (client_err != SSL_ERROR_NONE && client_err != SSL_ERROR_WANT_READ &&
          client_err != SSL_ERROR_WANT_WRITE) {
        return absl::InternalError(absl::StrCat("Client error:", client_err));
      }

      int server_ret = SSL_do_handshake(server);
      int server_err = SSL_get_error(server, server_ret);
      if (server_err != SSL_ERROR_NONE && server_err != SSL_ERROR_WANT_READ &&
          server_err != SSL_ERROR_WANT_WRITE) {
        return absl::InternalError(absl::StrCat("Server error:", server_err));
      }
      if (client_ret == 1 && server_ret == 1) {
        break;
      }
    }

    *out_client = client;
    *out_server = server;

    return absl::OkStatus();
  }

  static std::size_t CalculateRecordSizeFromHeader(uint8_t fourth_header_byte,
                                                   uint8_t fifth_header_byte) {
    return (static_cast<int>(fourth_header_byte & 0xff) << 8) +
           static_cast<int>(fifth_header_byte & 0xff);
  }

  void SetUp() override {
    ASSERT_EQ(DoHandshake(&client_ssl, &server_ssl), absl::OkStatus());

    ASSERT_THAT(client_ssl, NotNull());
    ASSERT_THAT(server_ssl, NotNull());

    BIO* client_ssl_bio = nullptr;
    ASSERT_EQ(BIO_new_bio_pair(&client_bio, /*writebuf1=*/0, &client_ssl_bio,
                               /*writebuf2=*/0),
              1);
    SSL_set_bio(client_ssl, client_ssl_bio, client_ssl_bio);

    BIO* server_ssl_bio = nullptr;
    ASSERT_EQ(BIO_new_bio_pair(&server_bio, /*writebuf1=*/0, &server_ssl_bio,
                               /*writebuf2=*/0),
              1);
    SSL_set_bio(server_ssl, server_ssl_bio, server_ssl_bio);

    client_buffer_offset = 0;
    client_buffer.resize(kMaxPlaintextBytesPerTlsRecord);
    server_buffer_offset = 0;
    server_buffer.resize(kMaxPlaintextBytesPerTlsRecord);
  }

  void TearDown() override {
    BIO_free(client_bio);
    SSL_free(client_ssl);
    BIO_free(server_bio);
    SSL_free(server_ssl);
  }

  SSL* client_ssl;
  BIO* client_bio;
  std::vector<uint8_t> client_buffer;
  std::size_t client_buffer_offset;

  SSL* server_ssl;
  BIO* server_bio;
  std::vector<uint8_t> server_buffer;
  std::size_t server_buffer_offset;
};

// This test consists of two tests, namely for each combination of parameters,
// we create a message on one side, protect it (encrypt it), and send it to
// the other side for unprotecting (decrypting).
TEST_P(FlowTest,
       ClientMessageToServerCanBeProtectedAndUnprotectedSuccessfully) {
  std::vector<uint8_t> unprotected_bytes(GetParam().plaintext_size, 'a');
  std::size_t unprotected_bytes_size = unprotected_bytes.size();

  std::vector<uint8_t> protected_output_frames(
      GetParam().expected_encrypted_bytes_size);
  std::size_t protected_output_frames_size = protected_output_frames.size();

  EXPECT_EQ(SslProtectorProtect(unprotected_bytes.data(), client_buffer.size(),
                                client_buffer_offset, client_buffer.data(),
                                client_ssl, client_bio, &unprotected_bytes_size,
                                protected_output_frames.data(),
                                &protected_output_frames_size),
            tsi_result::TSI_OK);

  // If |GetParam().plaintext_size| is larger than the inner client_buffer size
  // (kMaxPlaintextBytesPerTlsRecord), then |Protect| will copy up to
  // |kMaxPlaintextBytesPerTlsRecord| bytes and output the protected
  // frame. Otherwise we need to manually flush the copied data in order
  // to get the protected frame.
  if (GetParam().plaintext_size >= kMaxPlaintextBytesPerTlsRecord) {
    EXPECT_EQ(unprotected_bytes_size, kMaxPlaintextBytesPerTlsRecord);
  } else {
    EXPECT_EQ(unprotected_bytes_size, GetParam().plaintext_size);
    EXPECT_EQ(protected_output_frames_size, 0);
    protected_output_frames_size = protected_output_frames.size();

    std::size_t still_pending_size = 0;
    EXPECT_EQ(SslProtectorProtectFlush(
                  client_buffer_offset, client_buffer.data(), client_ssl,
                  client_bio, protected_output_frames.data(),
                  &protected_output_frames_size, &still_pending_size),
              tsi_result::TSI_OK);
    EXPECT_EQ(still_pending_size, 0);
  }

  // The first three bytes are always 0x17, 0x03, 0x03.
  EXPECT_EQ(protected_output_frames[0], '\x17');
  EXPECT_EQ(protected_output_frames[1], '\x03');
  EXPECT_EQ(protected_output_frames[2], '\x03');
  // The next two bytes are the size of the record, which is 5 bytes less
  // than the size of the whole frame.
  EXPECT_EQ(CalculateRecordSizeFromHeader(protected_output_frames[3],
                                          protected_output_frames[4]),
            protected_output_frames_size - 5);

  std::vector<uint8_t> unprotected_output_bytes(GetParam().plaintext_size);
  std::size_t unprotected_output_bytes_size = unprotected_output_bytes.size();

  // This frame should be decrypted by peer correctly.
  EXPECT_EQ(SslProtectorUnprotect(protected_output_frames.data(), server_ssl,
                                  server_bio, &protected_output_frames_size,
                                  unprotected_output_bytes.data(),
                                  &unprotected_output_bytes_size),
            tsi_result::TSI_OK);
  EXPECT_EQ(unprotected_output_bytes_size, unprotected_bytes_size);
  unprotected_output_bytes.resize(unprotected_output_bytes_size);
  unprotected_bytes.resize(unprotected_bytes_size);
  EXPECT_THAT(unprotected_output_bytes, ContainerEq(unprotected_bytes));
}

TEST_P(FlowTest,
       ServerMessageToClientCanBeProtectedAndUnprotectedSuccessfully) {
  std::vector<uint8_t> unprotected_bytes(GetParam().plaintext_size, 'a');
  std::size_t unprotected_bytes_size = unprotected_bytes.size();

  std::vector<uint8_t> protected_output_frames(
      GetParam().expected_encrypted_bytes_size);
  std::size_t protected_output_frames_size = protected_output_frames.size();

  EXPECT_EQ(SslProtectorProtect(unprotected_bytes.data(), server_buffer.size(),
                                server_buffer_offset, server_buffer.data(),
                                server_ssl, server_bio, &unprotected_bytes_size,
                                protected_output_frames.data(),
                                &protected_output_frames_size),
            tsi_result::TSI_OK);

  // If |GetParam().plaintext_size| is larger than the inner server_buffer size
  // (kMaxPlaintextBytesPerTlsRecord), then |Protect| will copy up to
  // |kMaxPlaintextBytesPerTlsRecord| bytes and output the protected
  // frame. Otherwise we need to manually flush the copied data in order
  // to get the protected frame.
  if (GetParam().plaintext_size >= kMaxPlaintextBytesPerTlsRecord) {
    EXPECT_EQ(unprotected_bytes_size, kMaxPlaintextBytesPerTlsRecord);
  } else {
    EXPECT_EQ(unprotected_bytes_size, GetParam().plaintext_size);
    EXPECT_EQ(protected_output_frames_size, 0);
    protected_output_frames_size = protected_output_frames.size();

    std::size_t still_pending_size = 0;
    EXPECT_EQ(SslProtectorProtectFlush(
                  server_buffer_offset, server_buffer.data(), server_ssl,
                  server_bio, protected_output_frames.data(),
                  &protected_output_frames_size, &still_pending_size),
              tsi_result::TSI_OK);
    EXPECT_EQ(still_pending_size, 0);
  }

  // The first three bytes are always 0x17, 0x03, 0x03.
  EXPECT_EQ(protected_output_frames[0], '\x17');
  EXPECT_EQ(protected_output_frames[1], '\x03');
  EXPECT_EQ(protected_output_frames[2], '\x03');
  // The next two bytes are the size of the record, which is 5 bytes less
  // than the size of the whole frame.
  EXPECT_EQ(CalculateRecordSizeFromHeader(protected_output_frames[3],
                                          protected_output_frames[4]),
            protected_output_frames_size - 5);

  std::vector<uint8_t> unprotected_output_bytes(GetParam().plaintext_size);
  std::size_t unprotected_output_bytes_size = unprotected_output_bytes.size();

  // This frame should be decrypted by peer correctly.
  EXPECT_EQ(SslProtectorUnprotect(protected_output_frames.data(), client_ssl,
                                  client_bio, &protected_output_frames_size,
                                  unprotected_output_bytes.data(),
                                  &unprotected_output_bytes_size),
            tsi_result::TSI_OK);
  EXPECT_EQ(unprotected_output_bytes_size, unprotected_bytes_size);
  unprotected_output_bytes.resize(unprotected_output_bytes_size);
  unprotected_bytes.resize(unprotected_bytes_size);
  EXPECT_THAT(unprotected_output_bytes, ContainerEq(unprotected_bytes));
}

INSTANTIATE_TEST_SUITE_P(FrameProtectorUtil, FlowTest,
                         ValuesIn(GenerateTestData()));

#endif  // OPENSSL_IS_BORINGSSL

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
