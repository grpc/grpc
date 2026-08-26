// Copyright 2025 gRPC authors.
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

// DPoP call credentials implementation (RFC 9449, EXPERIMENTAL).
//
// Per-RPC flow:
//   1. Require a TLS connection (https security connector; ssl auth context).
//   2. Require per-connection TLS channel binding from the auth context
//      (tls-unique / tls-exporter). This binds the proof to this TLS session.
//   3. Build htu from the TLS url scheme + authority + path.
//   4. Sign a DPoP proof JWT (ES256) covering htm, htu, ath, jti, iat,
//      and tls_channel_binding.
//   5. Attach "authorization: DPoP <token>" and "dpop: <proof>" to metadata.
//
// Cryptography: uses BoringSSL / OpenSSL EVP APIs already present in the tree.

#include "src/core/credentials/call/dpop/dpop_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/support/port_platform.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdlib.h>

#include <array>
#include <cstring>
#include <string>
#include <utility>

#include "src/core/call/metadata_batch.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/transport/security_connector.h"
#include "src/core/credentials/transport/tls/tls_utils.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/unique_type_name.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc_core {

namespace {

// Base64url-encode (no padding) a byte span.
std::string Base64UrlNoPad(absl::string_view in) {
  std::string out;
  absl::WebSafeBase64Escape(in, &out);
  while (!out.empty() && out.back() == '=') out.pop_back();
  return out;
}

// SHA-256 of |input|, returned as a base64url string (no padding).
std::string Sha256Base64Url(absl::string_view input) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(),
         digest);
  return Base64UrlNoPad(absl::string_view(
      reinterpret_cast<const char*>(digest), SHA256_DIGEST_LENGTH));
}

std::string JsonString(absl::string_view s) {
  return absl::StrCat("\"", s, "\"");
}

std::string BnToBase64Url(const BIGNUM* bn, int len) {
  std::string buf(len, '\0');
  BN_bn2binpad(bn, reinterpret_cast<unsigned char*>(&buf[0]), len);
  return Base64UrlNoPad(buf);
}

// Build a minimal RFC 7517 JWK object for an EC P-256 public key.
std::string BuildEcJwk(EVP_PKEY* pkey) {
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
  BIGNUM* x_bn = BN_new();
  BIGNUM* y_bn = BN_new();
  if (!EVP_PKEY_get_bn_param(pkey, "qX", &x_bn) ||
      !EVP_PKEY_get_bn_param(pkey, "qY", &y_bn)) {
    BN_free(x_bn);
    BN_free(y_bn);
    return "";
  }
  std::string x = BnToBase64Url(x_bn, 32);
  std::string y = BnToBase64Url(y_bn, 32);
  BN_free(x_bn);
  BN_free(y_bn);
#else
  const EC_KEY* ec = EVP_PKEY_get0_EC_KEY(pkey);
  if (ec == nullptr) return "";
  const EC_GROUP* group = EC_KEY_get0_group(ec);
  const EC_POINT* point = EC_KEY_get0_public_key(ec);
  BIGNUM* x_bn = BN_new();
  BIGNUM* y_bn = BN_new();
  if (!EC_POINT_get_affine_coordinates_GFp(group, point, x_bn, y_bn, nullptr)) {
    BN_free(x_bn);
    BN_free(y_bn);
    return "";
  }
  std::string x = BnToBase64Url(x_bn, 32);
  std::string y = BnToBase64Url(y_bn, 32);
  BN_free(x_bn);
  BN_free(y_bn);
#endif
  return absl::StrCat("{", JsonString("kty"), ":", JsonString("EC"), ",",
                      JsonString("crv"), ":", JsonString("P-256"), ",",
                      JsonString("x"), ":", JsonString(x), ",",
                      JsonString("y"), ":", JsonString(y), "}");
}

// Sign |to_sign| with |pkey| using ECDSA/SHA-256.
// Returns IEEE P1363 (r||s) encoded signature as base64url, or empty on error.
std::string EcdsaSign(EVP_PKEY* pkey, absl::string_view to_sign) {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (ctx == nullptr) return "";

  if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";
  }
  if (EVP_DigestSignUpdate(ctx, to_sign.data(), to_sign.size()) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";
  }

  size_t der_len = 0;
  if (EVP_DigestSignFinal(ctx, nullptr, &der_len) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";
  }
  std::string der(der_len, '\0');
  if (EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char*>(&der[0]),
                          &der_len) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";
  }
  EVP_MD_CTX_free(ctx);
  der.resize(der_len);

  const unsigned char* p = reinterpret_cast<const unsigned char*>(der.data());
  ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(der.size()));
  if (sig == nullptr) return "";

  const BIGNUM* r;
  const BIGNUM* s;
  ECDSA_SIG_get0(sig, &r, &s);
  std::string rs(64, '\0');
  BN_bn2binpad(r, reinterpret_cast<unsigned char*>(&rs[0]), 32);
  BN_bn2binpad(s, reinterpret_cast<unsigned char*>(&rs[32]), 32);
  ECDSA_SIG_free(sig);
  return Base64UrlNoPad(rs);
}

std::string GenJti() {
  std::array<unsigned char, 16> buf{};
  if (RAND_bytes(buf.data(), buf.size()) != 1) {
    // Extremely unlikely; still produce a unique-ish value.
    int64_t now = absl::ToUnixNanos(absl::Now());
    memcpy(buf.data(), &now, sizeof(now));
  }
  return Base64UrlNoPad(
      absl::string_view(reinterpret_cast<const char*>(buf.data()), buf.size()));
}

// True when the call is bound to a TLS connection.
bool IsTlsBoundConnection(
    const grpc_call_credentials::GetRequestMetadataArgs* args) {
  if (args == nullptr || args->security_connector == nullptr) {
    return false;
  }
  if (args->security_connector->url_scheme() != GRPC_SSL_URL_SCHEME) {
    return false;
  }
  if (args->auth_context != nullptr) {
    absl::string_view security_type = GetAuthPropertyValue(
        args->auth_context.get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME);
    if (!security_type.empty() &&
        security_type != GRPC_SSL_TRANSPORT_SECURITY_TYPE) {
      return false;
    }
  }
  return true;
}

// Per-connection TLS channel binding ("type:base64url") from auth context.
std::string GetTlsChannelBinding(
    const grpc_call_credentials::GetRequestMetadataArgs* args) {
  if (args == nullptr || args->auth_context == nullptr) return "";
  absl::string_view value = GetAuthPropertyValue(
      args->auth_context.get(), GRPC_TLS_CHANNEL_BINDING_PROPERTY_NAME);
  return std::string(value);
}

// Build DPoP htu = <tls-scheme>://<authority><path>, stripping :443 for https.
std::string MakeDpopHtu(
    const ClientMetadataHandle& initial_metadata,
    const grpc_call_credentials::GetRequestMetadataArgs* args) {
  auto* path_md = initial_metadata->get_pointer(HttpPathMetadata());
  auto* authority_md = initial_metadata->get_pointer(HttpAuthorityMetadata());
  if (path_md == nullptr || authority_md == nullptr) {
    return "";
  }
  absl::string_view path = path_md->as_string_view();
  absl::string_view host_and_port = authority_md->as_string_view();
  absl::string_view url_scheme = args->security_connector->url_scheme();
  if (url_scheme == GRPC_SSL_URL_SCHEME) {
    auto port_delimiter = host_and_port.find_last_of(':');
    if (port_delimiter != absl::string_view::npos &&
        host_and_port.substr(port_delimiter + 1) == "443") {
      host_and_port = host_and_port.substr(0, port_delimiter);
    }
  }
  return absl::StrCat(url_scheme, "://", host_and_port, path);
}

}  // namespace

grpc_dpop_credentials::grpc_dpop_credentials(absl::string_view access_token,
                                             absl::string_view ec_private_pem)
    : authorization_value_(Slice::FromCopiedString(
          absl::StrCat(GRPC_DPOP_AUTHORIZATION_SCHEME, access_token))),
      ec_private_pem_(ec_private_pem) {}

grpc_dpop_credentials::~grpc_dpop_credentials() = default;

UniqueTypeName grpc_dpop_credentials::Type() {
  static UniqueTypeName::Factory kFactory("DPoP");
  return kFactory.Create();
}

std::string grpc_dpop_credentials::BuildDpopProof(
    absl::string_view htu, absl::string_view tls_channel_binding) const {
  BIO* bio = BIO_new_mem_buf(ec_private_pem_.data(),
                             static_cast<int>(ec_private_pem_.size()));
  if (bio == nullptr) {
    LOG(ERROR) << "DPoP: BIO_new_mem_buf failed";
    return "";
  }
  EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (pkey == nullptr) {
    LOG(ERROR) << "DPoP: failed to parse EC private key PEM";
    return "";
  }

  std::string jwk = BuildEcJwk(pkey);
  if (jwk.empty()) {
    EVP_PKEY_free(pkey);
    LOG(ERROR) << "DPoP: failed to build JWK from public key";
    return "";
  }

  std::string header_json =
      absl::StrCat("{", JsonString("typ"), ":", JsonString("dpop+jwt"), ",",
                   JsonString("alg"), ":", JsonString("ES256"), ",",
                   JsonString("jwk"), ":", jwk, "}");
  std::string header_b64 = Base64UrlNoPad(header_json);

  absl::string_view token_with_scheme = authorization_value_.as_string_view();
  absl::string_view raw_token = token_with_scheme.substr(
      sizeof(GRPC_DPOP_AUTHORIZATION_SCHEME) - 1);  // strip "DPoP "
  std::string ath = Sha256Base64Url(raw_token);
  std::string jti = GenJti();
  int64_t iat = absl::ToUnixSeconds(absl::Now());

  // tls_channel_binding is unique to this TLS session. Including it in the
  // signed claims prevents replaying the proof on a different TLS connection.
  std::string claims_json = absl::StrCat(
      "{", JsonString("htm"), ":", JsonString("POST"), ",", JsonString("htu"),
      ":", JsonString(htu), ",", JsonString("ath"), ":", JsonString(ath), ",",
      JsonString("jti"), ":", JsonString(jti), ",", JsonString("iat"), ":",
      std::to_string(iat), ",", JsonString("tls_channel_binding"), ":",
      JsonString(tls_channel_binding), "}");
  std::string claims_b64 = Base64UrlNoPad(claims_json);

  std::string to_sign = absl::StrCat(header_b64, ".", claims_b64);
  std::string sig = EcdsaSign(pkey, to_sign);
  EVP_PKEY_free(pkey);

  if (sig.empty()) {
    LOG(ERROR) << "DPoP: ECDSA signing failed";
    return "";
  }

  return absl::StrCat(to_sign, ".", sig);
}

ArenaPromise<absl::StatusOr<ClientMetadataHandle>>
grpc_dpop_credentials::GetRequestMetadata(
    ClientMetadataHandle initial_metadata, const GetRequestMetadataArgs* args) {
  // Token binding: DPoP proofs are only emitted on TLS-secured connections.
  if (!IsTlsBoundConnection(args)) {
    return Immediate(absl::UnauthenticatedError(
        "DPoP credentials require a TLS-secured connection"));
  }

  // Session binding: require per-connection channel binding so the proof
  // cannot be replayed on another TLS session.
  std::string tls_channel_binding = GetTlsChannelBinding(args);
  if (tls_channel_binding.empty()) {
    return Immediate(absl::UnauthenticatedError(
        "DPoP credentials require TLS channel binding for this connection"));
  }

  std::string htu = MakeDpopHtu(initial_metadata, args);
  if (htu.empty()) {
    return Immediate(absl::UnauthenticatedError(
        "DPoP: missing authority or path for htu"));
  }

  std::string proof = BuildDpopProof(htu, tls_channel_binding);
  if (proof.empty()) {
    return Immediate(
        absl::UnauthenticatedError("DPoP: failed to build proof JWT"));
  }

  initial_metadata->Append(
      GRPC_AUTHORIZATION_METADATA_KEY, authorization_value_.Ref(),
      [](absl::string_view, const Slice&) { abort(); });

  initial_metadata->Append(
      GRPC_DPOP_PROOF_METADATA_KEY, Slice::FromCopiedString(proof),
      [](absl::string_view, const Slice&) { abort(); });

  return Immediate(std::move(initial_metadata));
}

}  // namespace grpc_core

grpc_call_credentials* grpc_dpop_credentials_create(const char* access_token,
                                                    const char* ec_private_pem,
                                                    void* reserved) {
  (void)reserved;
  if (access_token == nullptr || access_token[0] == '\0') {
    LOG(ERROR) << "grpc_dpop_credentials_create: access_token must not be empty";
    return nullptr;
  }
  if (ec_private_pem == nullptr || ec_private_pem[0] == '\0') {
    LOG(ERROR)
        << "grpc_dpop_credentials_create: ec_private_pem must not be empty";
    return nullptr;
  }
  return grpc_core::MakeRefCounted<grpc_core::grpc_dpop_credentials>(
             access_token, ec_private_pem)
      .release();
}
