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

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <array>
#include <string>
#include <vector>

#include "src/core/lib/slice/slice.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/load_file.h"
#include "test/core/test_util/test_config.h"
#include "test/core/tsi/transport_security_test_lib.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {

using ::testing::ContainerEq;
using ::testing::NotNull;
using ::testing::TestWithParam;
using ::testing::ValuesIn;

const char* kValidCrl =
    "test/core/tsi/test_creds/crl_data/crls/current.crl";
const char* kCrlIssuer =
    "test/core/tsi/test_creds/crl_data/ca.pem";
const char* kModifiedSignature =
    "test/core/tsi/test_creds/crl_data/bad_crls/"
    "invalid_signature.crl";
const char* kModifiedContent =
    "test/core/tsi/test_creds/crl_data/bad_crls/"
    "invalid_content.crl";
const char* kIntermediateCrl =
    "test/core/tsi/test_creds/crl_data/crls/intermediate.crl";
const char* kIntermediateCrlIssuer =
    "test/core/tsi/test_creds/crl_data/intermediate_ca.pem";
const char* kLeafCert =
    "test/core/tsi/test_creds/crl_data/"
    "leaf_signed_by_intermediate.pem";
const char* kEvilCa =
    "test/core/tsi/test_creds/crl_data/evil_ca.pem";
const char* kCaWithAkid =
    "test/core/tsi/test_creds/crl_data/ca_with_akid.pem";
const char* kCrlWithAkid =
    "test/core/tsi/test_creds/crl_data/crl_with_akid.crl";

constexpr absl::string_view kLeafCertPem =
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
    "-----END CERTIFICATE-----";
constexpr absl::string_view kPrivateKeyPem =
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
    "-----END RSA PRIVATE KEY-----";
constexpr absl::string_view kEcPrivateKeyPem =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgOM7iHjJw/N6n8HtM\n"
    "bVVVRhEYXoHFF+MSaTYQxOWM1p+hRANCAASMeWC+pIJAm/1fn0Wz3yyWGQzVPm9v\n"
    "LCQo5JvK0a2t+Aa6d3AtLRwo6vh1VbJ8zFZxxIwyJNis3n1jRMWal7Vo\n"
    "-----END PRIVATE KEY-----";
constexpr absl::string_view kRsaPrivateKeyPem =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCqyrzsrS8mWQwz\n"
    "VFudLgte2kJX/pZ3KqJQBtMrkLxpgyJJU8mVBB+quDwnfH6PnQk+sF9omTlGAAxR\n"
    "JzSEe8BS1Wnxld6rr6o/381VVW/2b+2kSifCtx/gVwCQQLnf4dbjfGW7ZClu1URG\n"
    "ks2lK9T9BIh9SMSnYLEKEC8sWW1LibzJxHapFjIP88GrqgpPNGdEK7ABMsqHASuU\n"
    "MvQ+0w7sdX2Pdu+Gm8ChxawvLiQVSh9ehtJiPl/jWbcZ6K3caTUxMf9tn8ky0DMK\n"
    "xmHHmmxu19ehegzi7KSzjHmJ4QAtrtDaB/+ud0ZJ5l+pwfk7DL1TRjFYOyPVpExb\n"
    "nLcQQxzfAgMBAAECggEATc+kFygnzQ7Q0iniu0+Y+pPxmelxX8VawZ76YmTEkkWe\n"
    "P04fDvcb/kmFjm/XsVJYPelY7mywfUXUVrzH3nwK+TIl3FztX8beh89M203bfqkr\n"
    "2ae3Sazopuq8ZPw4MtnPb0DjkGZnwgkD3CtR6ah4lvWTwZB/l8ojnnQVKd1sP/c4\n"
    "LQSlVm2aiD6+D/NxbyJ4AOMWgUFrWBKqnV30mTZ5Lwv8fjznopgWMfsUl+Nx/HzV\n"
    "J1ZRtLE+Z9euFJOUeMSEG1+YFxXAA3XuRdY/4PpzvK8Rlxb2rtJvt+dHojQCz66U\n"
    "6PcspPt6MOcUFnpamJ513oKDwmdR8puRg7/bk2VKYQKBgQDVHz/NQaS8czTCkx8p\n"
    "jXmZcGv1DH+T3SWeNI871BXQbSSKuqrOXDfzfrIFK7uXAWtIAOKLVaZOcSEDk+Rj\n"
    "kbifkqRZuMy+iLdBLj/Gw3xVfkOb3m4g7OqWc7RBlfTCTCCUTVPiQkKZLGJ/eIJx\n"
    "sGvdyJP6f12MODqUobgQC2UniQKBgQDNJ0vDHdqRQYI4zz1gAYDaCruRjtwWvRSL\n"
    "tcBFlcVnMXjfZpxOKnGU1xEO4wDhEXra9yLwi/6IwGFtk0Zi2C9rYptniiURReuX\n"
    "TkNNf1JmyZhYuSXD9Pg1Ssa/t3ZtauFzK1rHL1R1UB/pnD8xxuB4aAl+kZKi1Ie+\n"
    "E6IXHuyfJwKBgQDOac+viq503tfww/Fgm2d0lw/YbNx7Z6rxiVJYzda64ZqMyrJ3\n"
    "35VJPiJJI8wyOuue90xzSuch/ivNfUWssgwwcSTAyV10BJIIjTSz283mN75fjpT3\n"
    "Sr8CLNoe05AVRwoe2K4v66D5HaXgc+VTG129lnDMIuOF1UfXgLH2yDKWkQKBgQC4\n"
    "ajqQiqWPLXQB3UkupCtP1ZYGooT1a8KsVBUieB+bQ72EFJktKrovMaUD3MtNhokJ\n"
    "jF68HRwRkd4CwgDjmbIGtf08ddIcVN4ShSe64lkQTOfF2ak5HVyBi1ZdwG2Urh87\n"
    "iB1yL/mb+wq01N95v2zIz7y5KeLGvIXJN5zda88IwQKBgFLk68ZMEDMVCLpdvywb\n"
    "bRC3rOl2CTHfXFD6RY0SIv4De+w7iQkYOn+4NIyG+hMfGfj5ooOO5gbsDyGagZqV\n"
    "OLc6cW5HnwN+PERByn+hSoyGq8IOk8Vn5DeV7PoqIlbbdfUmTUx69EtzvViZoa+O\n"
    "O2XDljPcjgc+pobqzebPIR6R\n"
    "-----END PRIVATE KEY-----";

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

    BIO* client_cert_bio(
        BIO_new_mem_buf(kLeafCertPem.data(), kLeafCertPem.size()));
    X509* client_cert = PEM_read_bio_X509(client_cert_bio, /*x=*/nullptr,
                                          /*cb=*/nullptr, /*u=*/nullptr);
    BIO* client_key_bio(
        BIO_new_mem_buf(kPrivateKeyPem.data(), kPrivateKeyPem.size()));
    EVP_PKEY* client_key =
        PEM_read_bio_PrivateKey(client_key_bio, /*x=*/nullptr,
                                /*cb=*/nullptr, /*u=*/nullptr);

    BIO* server_cert_bio(
        BIO_new_mem_buf(kLeafCertPem.data(), kLeafCertPem.size()));
    X509* server_cert = PEM_read_bio_X509(server_cert_bio, /*x=*/nullptr,
                                          /*cb=*/nullptr, /*u=*/nullptr);
    BIO* server_key_bio(
        BIO_new_mem_buf(kPrivateKeyPem.data(), kPrivateKeyPem.size()));
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

  // If |GetParam().plaintext_size| is larger than the inner client_buffer
  // size (kMaxPlaintextBytesPerTlsRecord), then |Protect| will copy up to
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

  // If |GetParam().plaintext_size| is larger than the inner server_buffer
  // size (kMaxPlaintextBytesPerTlsRecord), then |Protect| will copy up to
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

class CrlUtils : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
#if OPENSSL_VERSION_NUMBER >= 0x10100000
    OPENSSL_init_ssl(/*opts=*/0, /*settings=*/nullptr);
#else
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
#endif
  }

  void SetUp() override {
    absl::StatusOr<Slice> root_crl = LoadFile(kValidCrl, false);
    ASSERT_EQ(root_crl.status(), absl::OkStatus()) << root_crl.status();
    root_crl_ = ReadCrl(root_crl->as_string_view());
    absl::StatusOr<Slice> intermediate_crl = LoadFile(kIntermediateCrl, false);
    ASSERT_EQ(intermediate_crl.status(), absl::OkStatus())
        << intermediate_crl.status();
    intermediate_crl_ = ReadCrl(intermediate_crl->as_string_view());
    absl::StatusOr<Slice> invalid_signature_crl =
        LoadFile(kModifiedSignature, false);
    ASSERT_EQ(invalid_signature_crl.status(), absl::OkStatus())
        << invalid_signature_crl.status();
    invalid_signature_crl_ = ReadCrl(invalid_signature_crl->as_string_view());
    absl::StatusOr<Slice> akid_crl = LoadFile(kCrlWithAkid, false);
    ASSERT_EQ(akid_crl.status(), absl::OkStatus()) << akid_crl.status();
    akid_crl_ = ReadCrl(akid_crl->as_string_view());

    absl::StatusOr<Slice> root_ca = LoadFile(kCrlIssuer, false);
    ASSERT_EQ(root_ca.status(), absl::OkStatus());
    root_ca_ = ReadPemCert(root_ca->as_string_view());
    absl::StatusOr<Slice> intermediate_ca =
        LoadFile(kIntermediateCrlIssuer, false);
    ASSERT_EQ(intermediate_ca.status(), absl::OkStatus());
    intermediate_ca_ = ReadPemCert(intermediate_ca->as_string_view());
    absl::StatusOr<Slice> leaf_cert = LoadFile(kLeafCert, false);
    ASSERT_EQ(leaf_cert.status(), absl::OkStatus());
    leaf_cert_ = ReadPemCert(leaf_cert->as_string_view());
    absl::StatusOr<Slice> evil_ca = LoadFile(kEvilCa, false);
    ASSERT_EQ(evil_ca.status(), absl::OkStatus());
    evil_ca_ = ReadPemCert(evil_ca->as_string_view());
    absl::StatusOr<Slice> ca_with_akid = LoadFile(kCaWithAkid, false);
    ASSERT_EQ(ca_with_akid.status(), absl::OkStatus());
    ca_with_akid_ = ReadPemCert(ca_with_akid->as_string_view());
  }

  void TearDown() override {
    X509_CRL_free(root_crl_);
    X509_CRL_free(intermediate_crl_);
    X509_CRL_free(invalid_signature_crl_);
    X509_CRL_free(akid_crl_);
    X509_free(root_ca_);
    X509_free(intermediate_ca_);
    X509_free(leaf_cert_);
    X509_free(evil_ca_);
    X509_free(ca_with_akid_);
  }

 protected:
  X509_CRL* root_crl_;
  X509_CRL* intermediate_crl_;
  X509_CRL* invalid_signature_crl_;
  X509_CRL* akid_crl_;
  X509* root_ca_;
  X509* intermediate_ca_;
  X509* leaf_cert_;
  X509* evil_ca_;
  X509* ca_with_akid_;
};

TEST_F(CrlUtils, VerifySignatureValid) {
  EXPECT_TRUE(VerifyCrlSignature(root_crl_, root_ca_));
}

TEST_F(CrlUtils, VerifySignatureIntermediateValid) {
  EXPECT_TRUE(VerifyCrlSignature(intermediate_crl_, intermediate_ca_));
}

TEST_F(CrlUtils, VerifySignatureModifiedSignature) {
  EXPECT_FALSE(VerifyCrlSignature(invalid_signature_crl_, root_ca_));
}

TEST_F(CrlUtils, VerifySignatureModifiedContent) {
  absl::StatusOr<Slice> crl_slice = LoadFile(kModifiedContent, false);
  ASSERT_EQ(crl_slice.status(), absl::OkStatus()) << crl_slice.status();
  X509_CRL* crl = ReadCrl(crl_slice->as_string_view());
  EXPECT_EQ(crl, nullptr);
}

TEST_F(CrlUtils, VerifySignatureWrongIssuer) {
  EXPECT_FALSE(VerifyCrlSignature(root_crl_, intermediate_ca_));
}

TEST_F(CrlUtils, VerifySignatureWrongIssuer2) {
  EXPECT_FALSE(VerifyCrlSignature(intermediate_crl_, root_ca_));
}

TEST_F(CrlUtils, VerifySignatureNullCrl) {
  EXPECT_FALSE(VerifyCrlSignature(nullptr, root_ca_));
}

TEST_F(CrlUtils, VerifySignatureNullCert) {
  EXPECT_FALSE(VerifyCrlSignature(intermediate_crl_, nullptr));
}

TEST_F(CrlUtils, VerifySignatureNullCrlAndCert) {
  EXPECT_FALSE(VerifyCrlSignature(nullptr, nullptr));
}

TEST_F(CrlUtils, VerifyIssuerNamesMatch) {
  EXPECT_TRUE(VerifyCrlCertIssuerNamesMatch(root_crl_, root_ca_));
}

TEST_F(CrlUtils, VerifyIssuerNamesDontMatch) {
  EXPECT_FALSE(VerifyCrlCertIssuerNamesMatch(root_crl_, leaf_cert_));
}

TEST_F(CrlUtils, DuplicatedIssuerNamePassesButSignatureCheckFails) {
  // The issuer names will match, but it should fail a signature check
  EXPECT_TRUE(VerifyCrlCertIssuerNamesMatch(root_crl_, evil_ca_));
  EXPECT_FALSE(VerifyCrlSignature(root_crl_, evil_ca_));
}

TEST_F(CrlUtils, VerifyIssuerNameNullCrl) {
  EXPECT_FALSE(VerifyCrlCertIssuerNamesMatch(nullptr, root_ca_));
}

TEST_F(CrlUtils, VerifyIssuerNameNullCert) {
  EXPECT_FALSE(VerifyCrlCertIssuerNamesMatch(intermediate_crl_, nullptr));
}

TEST_F(CrlUtils, VerifyIssuerNameNullCrlAndCert) {
  EXPECT_FALSE(VerifyCrlCertIssuerNamesMatch(nullptr, nullptr));
}

TEST_F(CrlUtils, HasCrlSignBitExists) { EXPECT_TRUE(HasCrlSignBit(root_ca_)); }

TEST_F(CrlUtils, HasCrlSignBitMissing) {
  EXPECT_FALSE(HasCrlSignBit(leaf_cert_));
}

TEST_F(CrlUtils, HasCrlSignBitNullCert) {
  EXPECT_FALSE(HasCrlSignBit(nullptr));
}

TEST_F(CrlUtils, IssuerFromIntermediateCert) {
  auto issuer = IssuerFromCert(intermediate_ca_);
  // Build the known name for comparison
  unsigned char* buf = nullptr;
  X509_NAME* expected_issuer_name = X509_NAME_new();
  ASSERT_TRUE(
      X509_NAME_add_entry_by_txt(expected_issuer_name, "C", MBSTRING_ASC,
                                 (const unsigned char*)"AU", -1, -1, 0));
  ASSERT_TRUE(X509_NAME_add_entry_by_txt(
      expected_issuer_name, "ST", MBSTRING_ASC,
      (const unsigned char*)"Some-State", -1, -1, 0));
  ASSERT_TRUE(X509_NAME_add_entry_by_txt(
      expected_issuer_name, "O", MBSTRING_ASC,
      (const unsigned char*)"Internet Widgits Pty Ltd", -1, -1, 0));
  ASSERT_TRUE(
      X509_NAME_add_entry_by_txt(expected_issuer_name, "CN", MBSTRING_ASC,
                                 (const unsigned char*)"testca", -1, -1, 0));
  int len = i2d_X509_NAME(expected_issuer_name, &buf);
  std::string expected_issuer_name_der(reinterpret_cast<char const*>(buf), len);
  OPENSSL_free(buf);
  X509_NAME_free(expected_issuer_name);
  ASSERT_EQ(issuer.status(), absl::OkStatus());
  EXPECT_EQ(*issuer, expected_issuer_name_der);
}

TEST_F(CrlUtils, IssuerFromLeaf) {
  auto issuer = IssuerFromCert(leaf_cert_);
  // Build the known name for comparison
  unsigned char* buf = nullptr;
  X509_NAME* expected_issuer_name = X509_NAME_new();
  ASSERT_TRUE(X509_NAME_add_entry_by_txt(
      expected_issuer_name, "CN", MBSTRING_ASC,
      (const unsigned char*)"intermediatecert.example.com", -1, -1, 0));
  int len = i2d_X509_NAME(expected_issuer_name, &buf);
  std::string expected_issuer_name_der(reinterpret_cast<char const*>(buf), len);
  OPENSSL_free(buf);
  X509_NAME_free(expected_issuer_name);
  ASSERT_EQ(issuer.status(), absl::OkStatus());
  EXPECT_EQ(*issuer, expected_issuer_name_der);
}

TEST_F(CrlUtils, IssuerFromCertNull) {
  auto issuer = IssuerFromCert(nullptr);
  EXPECT_EQ(issuer.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(CrlUtils, CertCrlAkidValid) {
  auto akid = AkidFromCertificate(ca_with_akid_);
  EXPECT_EQ(akid.status(), absl::OkStatus());
  auto crl_akid = AkidFromCrl(akid_crl_);
  EXPECT_EQ(crl_akid.status(), absl::OkStatus());
  EXPECT_NE(*akid, "");
  // It's easiest to compare that these two pull the same value, it's very
  // difficult to create the known AKID value as a test constant, so we just
  // check that they are not empty and that they are the same.
  EXPECT_EQ(*akid, *crl_akid);
}

TEST_F(CrlUtils, CertNoAkid) {
  auto akid = AkidFromCertificate(root_ca_);
  EXPECT_EQ(akid.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(CrlUtils, CrlNoAkid) {
  auto akid = AkidFromCrl(root_crl_);
  EXPECT_EQ(akid.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(CrlUtils, CertAkidNullptr) {
  auto akid = AkidFromCertificate(nullptr);
  EXPECT_EQ(akid.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(CrlUtils, CrlAkidNullptr) {
  auto akid = AkidFromCrl(nullptr);
  EXPECT_EQ(akid.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ParsePemCertificateChainTest, EmptyPem) {
  EXPECT_EQ(ParsePemCertificateChain(/*cert_chain_pem=*/"").status(),
            absl::InvalidArgumentError("Cert chain PEM is empty."));
}

TEST(ParsePemCertificateChainTest, InvalidPem) {
  EXPECT_EQ(ParsePemCertificateChain("invalid-pem").status(),
            absl::NotFoundError("No certificates found."));
}

TEST(ParsePemCertificateChainTest, PartialPem) {
  std::string pem(kLeafCertPem);
  EXPECT_EQ(ParsePemCertificateChain(pem.substr(0, pem.length() / 2)).status(),
            absl::FailedPreconditionError("Invalid PEM."));
}

TEST(ParsePemCertificateChainTest, SingleCertSuccess) {
  absl::StatusOr<std::vector<X509*>> certs =
      ParsePemCertificateChain(kLeafCertPem);
  EXPECT_EQ(certs.status(), absl::OkStatus());
  EXPECT_EQ(certs->size(), 1);
  EXPECT_NE(certs->at(0), nullptr);
  X509_free(certs->at(0));
}

TEST(ParsePemCertificateChainTest, MultipleCertFailure) {
  EXPECT_EQ(ParsePemCertificateChain(absl::StrCat(kLeafCertPem, kLeafCertPem))
                .status(),
            absl::FailedPreconditionError("Invalid PEM."));
}

TEST(ParsePemCertificateChainTest, MultipleCertSuccess) {
  absl::StatusOr<std::vector<X509*>> certs =
      ParsePemCertificateChain(absl::StrCat(kLeafCertPem, "\n", kLeafCertPem));
  EXPECT_EQ(certs.status(), absl::OkStatus());
  EXPECT_EQ(certs->size(), 2);
  EXPECT_NE(certs->at(0), nullptr);
  EXPECT_NE(certs->at(1), nullptr);
  X509_free(certs->at(0));
  X509_free(certs->at(1));
}

TEST(ParsePemCertificateChainTest, MultipleCertWithExtraMiddleLinesSuccess) {
  absl::StatusOr<std::vector<X509*>> certs = ParsePemCertificateChain(
      absl::StrCat(kLeafCertPem, "\nGarbage\n", kLeafCertPem));
  EXPECT_EQ(certs.status(), absl::OkStatus());
  EXPECT_EQ(certs->size(), 2);
  EXPECT_NE(certs->at(0), nullptr);
  EXPECT_NE(certs->at(1), nullptr);
  X509_free(certs->at(0));
  X509_free(certs->at(1));
}

TEST(ParsePemCertificateChainTest, MultipleCertWitManyMiddleLinesSuccess) {
  absl::StatusOr<std::vector<X509*>> certs = ParsePemCertificateChain(
      absl::StrCat(kLeafCertPem, "\n\n\n\n\n\n\n", kLeafCertPem));
  EXPECT_EQ(certs.status(), absl::OkStatus());
  EXPECT_EQ(certs->size(), 2);
  EXPECT_NE(certs->at(0), nullptr);
  EXPECT_NE(certs->at(1), nullptr);
  X509_free(certs->at(0));
  X509_free(certs->at(1));
}

TEST(ParsePemCertificateChainTest, ValidCertWithInvalidSuffix) {
  EXPECT_EQ(ParsePemCertificateChain(absl::StrCat(kLeafCertPem, "invalid-pem"))
                .status(),
            absl::FailedPreconditionError("Invalid PEM."));
}

TEST(ParsePemCertificateChainTest, ValidCertWithInvalidPrefix) {
  EXPECT_EQ(ParsePemCertificateChain(absl::StrCat("invalid-pem", kLeafCertPem))
                .status(),
            absl::NotFoundError("No certificates found."));
}

TEST(ParsePemCertificateChainTest, ValidCertWithInvalidLeadingLine) {
  absl::StatusOr<std::vector<X509*>> certs =
      ParsePemCertificateChain(absl::StrCat("invalid-pem\n", kLeafCertPem));
  EXPECT_EQ(certs.status(), absl::OkStatus());
  EXPECT_EQ(certs->size(), 1);
  EXPECT_NE(certs->at(0), nullptr);
  X509_free(certs->at(0));
}

TEST(ParsePemPrivateKeyTest, EmptyPem) {
  EXPECT_EQ(ParsePemPrivateKey(/*private_key_pem=*/"").status(),
            absl::NotFoundError("No private key found."));
}

TEST(ParsePemPrivateKeyTest, InvalidPem) {
  EXPECT_EQ(ParsePemPrivateKey("invalid-pem").status(),
            absl::NotFoundError("No private key found."));
}

TEST(ParsePemPrivateKeyTest, PartialPem) {
  std::string pem(kPrivateKeyPem);
  EXPECT_EQ(ParsePemPrivateKey(pem.substr(0, pem.length() / 2)).status(),
            absl::NotFoundError("No private key found."));
}

TEST(ParsePemPrivateKeyTest, RsaSuccess1) {
  absl::StatusOr<EVP_PKEY*> pkey = ParsePemPrivateKey(kPrivateKeyPem);
  EXPECT_EQ(pkey.status(), absl::OkStatus());
  EXPECT_NE(*pkey, nullptr);
  EVP_PKEY_free(*pkey);
}

TEST(ParsePemPrivateKeyTest, RsaSuccess2) {
  absl::StatusOr<EVP_PKEY*> pkey = ParsePemPrivateKey(kRsaPrivateKeyPem);
  EXPECT_EQ(pkey.status(), absl::OkStatus());
  EXPECT_NE(*pkey, nullptr);
  EVP_PKEY_free(*pkey);
}

TEST(ParsePemPrivateKeyTest, EcSuccess) {
  absl::StatusOr<EVP_PKEY*> pkey = ParsePemPrivateKey(kEcPrivateKeyPem);
  EXPECT_EQ(pkey.status(), absl::OkStatus());
  EXPECT_NE(*pkey, nullptr);
  EVP_PKEY_free(*pkey);
}
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
