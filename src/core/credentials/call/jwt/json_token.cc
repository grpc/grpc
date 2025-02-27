//
//
// Copyright 2015 gRPC authors.
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

#include "src/core/credentials/call/jwt/json_token.h"

#include <grpc/credentials.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "src/core/credentials/call/json_util.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"

using grpc_core::Json;

// --- Constants. ---

// 1 hour max.
gpr_timespec grpc_max_auth_token_lifetime() {
  gpr_timespec out;
  out.tv_sec = 3600;
  out.tv_nsec = 0;
  out.clock_type = GPR_TIMESPAN;
  return out;
}

#define GRPC_JWT_RSA_SHA256_ALGORITHM "RS256"
#define GRPC_JWT_TYPE "JWT"

// --- Override for testing. ---

static grpc_jwt_encode_and_sign_override g_jwt_encode_and_sign_override =
    nullptr;

// --- grpc_auth_json_key. ---

int grpc_auth_json_key_is_valid(const grpc_auth_json_key* json_key) {
  return (json_key != nullptr) &&
         strcmp(json_key->type, GRPC_AUTH_JSON_TYPE_INVALID) != 0;
}

grpc_auth_json_key grpc_auth_json_key_create_from_json(const Json& json) {
  grpc_auth_json_key result;
  BIO* bio = nullptr;
  const char* prop_value;
  int success = 0;
  grpc_error_handle error;

  memset(&result, 0, sizeof(grpc_auth_json_key));
  result.type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (json.type() == Json::Type::kNull) {
    LOG(ERROR) << "Invalid json.";
    goto end;
  }

  prop_value = grpc_json_get_string_property(json, "type", &error);
  GRPC_LOG_IF_ERROR("JSON key parsing", error);
  if (prop_value == nullptr ||
      strcmp(prop_value, GRPC_AUTH_JSON_TYPE_SERVICE_ACCOUNT) != 0) {
    goto end;
  }
  result.type = GRPC_AUTH_JSON_TYPE_SERVICE_ACCOUNT;

  if (!grpc_copy_json_string_property(json, "private_key_id",
                                      &result.private_key_id) ||
      !grpc_copy_json_string_property(json, "client_id", &result.client_id) ||
      !grpc_copy_json_string_property(json, "client_email",
                                      &result.client_email)) {
    goto end;
  }

  prop_value = grpc_json_get_string_property(json, "private_key", &error);
  GRPC_LOG_IF_ERROR("JSON key parsing", error);
  if (prop_value == nullptr) {
    goto end;
  }
  bio = BIO_new(BIO_s_mem());
  success = BIO_puts(bio, prop_value);
  if ((success < 0) || (static_cast<size_t>(success) != strlen(prop_value))) {
    LOG(ERROR) << "Could not write into openssl BIO.";
    goto end;
  }
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  result.private_key =
      PEM_read_bio_RSAPrivateKey(bio, nullptr, nullptr, const_cast<char*>(""));
#else
  result.private_key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
#endif
  if (result.private_key == nullptr) {
    LOG(ERROR) << "Could not deserialize private key.";
    goto end;
  }
  success = 1;

end:
  if (bio != nullptr) BIO_free(bio);
  if (!success) grpc_auth_json_key_destruct(&result);
  return result;
}

grpc_auth_json_key grpc_auth_json_key_create_from_string(
    const char* json_string) {
  Json json;
  auto json_or = grpc_core::JsonParse(json_string);
  if (!json_or.ok()) {
    LOG(ERROR) << "JSON key parsing error: " << json_or.status();
  } else {
    json = std::move(*json_or);
  }
  return grpc_auth_json_key_create_from_json(json);
}

void grpc_auth_json_key_destruct(grpc_auth_json_key* json_key) {
  if (json_key == nullptr) return;
  json_key->type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (json_key->client_id != nullptr) {
    gpr_free(json_key->client_id);
    json_key->client_id = nullptr;
  }
  if (json_key->private_key_id != nullptr) {
    gpr_free(json_key->private_key_id);
    json_key->private_key_id = nullptr;
  }
  if (json_key->client_email != nullptr) {
    gpr_free(json_key->client_email);
    json_key->client_email = nullptr;
  }
  if (json_key->private_key != nullptr) {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    RSA_free(json_key->private_key);
#else
    EVP_PKEY_free(json_key->private_key);
#endif
    json_key->private_key = nullptr;
  }
}

// --- jwt encoding and signature. ---

static char* encoded_jwt_header(const char* key_id, const char* algorithm) {
  Json json = Json::FromObject({
      {"alg", Json::FromString(algorithm)},
      {"typ", Json::FromString(GRPC_JWT_TYPE)},
      {"kid", Json::FromString(key_id)},
  });
  std::string json_str = grpc_core::JsonDump(json);
  return gpr_strdup(absl::WebSafeBase64Escape(json_str).c_str());
}

static char* encoded_jwt_claim(const grpc_auth_json_key* json_key,
                               const char* audience,
                               gpr_timespec token_lifetime, const char* scope) {
  gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
  gpr_timespec expiration = gpr_time_add(now, token_lifetime);
  if (gpr_time_cmp(token_lifetime, grpc_max_auth_token_lifetime()) > 0) {
    VLOG(2) << "Cropping token lifetime to maximum allowed value.";
    expiration = gpr_time_add(now, grpc_max_auth_token_lifetime());
  }

  Json::Object object = {
      {"iss", Json::FromString(json_key->client_email)},
      {"aud", Json::FromString(audience)},
      {"iat", Json::FromNumber(now.tv_sec)},
      {"exp", Json::FromNumber(expiration.tv_sec)},
  };
  if (scope != nullptr) {
    object["scope"] = Json::FromString(scope);
  } else {
    // Unscoped JWTs need a sub field.
    object["sub"] = Json::FromString(json_key->client_email);
  }

  std::string json_str =
      grpc_core::JsonDump(Json::FromObject(std::move(object)));
  return gpr_strdup(absl::WebSafeBase64Escape(json_str).c_str());
}

static char* dot_concat_and_free_strings(char* str1, char* str2) {
  size_t str1_len = strlen(str1);
  size_t str2_len = strlen(str2);
  size_t result_len = str1_len + 1 /* dot */ + str2_len;
  char* result =
      static_cast<char*>(gpr_malloc(result_len + 1 /* NULL terminated */));
  char* current = result;
  memcpy(current, str1, str1_len);
  current += str1_len;
  *(current++) = '.';
  memcpy(current, str2, str2_len);
  current += str2_len;
  CHECK(current >= result);
  CHECK((uintptr_t)(current - result) == result_len);
  *current = '\0';
  gpr_free(str1);
  gpr_free(str2);
  return result;
}

const EVP_MD* openssl_digest_from_algorithm(const char* algorithm) {
  if (strcmp(algorithm, GRPC_JWT_RSA_SHA256_ALGORITHM) == 0) {
    return EVP_sha256();
  } else {
    LOG(ERROR) << "Unknown algorithm " << algorithm;
    return nullptr;
  }
}

char* compute_and_encode_signature(const grpc_auth_json_key* json_key,
                                   const char* signature_algorithm,
                                   const char* to_sign) {
  const EVP_MD* md = openssl_digest_from_algorithm(signature_algorithm);
  EVP_MD_CTX* md_ctx = nullptr;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  EVP_PKEY* key = EVP_PKEY_new();
#endif
  size_t sig_len = 0;
  unsigned char* sig = nullptr;
  char* result = nullptr;
  if (md == nullptr) return nullptr;
  md_ctx = EVP_MD_CTX_create();
  if (md_ctx == nullptr) {
    LOG(ERROR) << "Could not create MD_CTX";
    goto end;
  }
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  EVP_PKEY_set1_RSA(key, json_key->private_key);
  if (EVP_DigestSignInit(md_ctx, nullptr, md, nullptr, key) != 1) {
#else
  if (EVP_DigestSignInit(md_ctx, nullptr, md, nullptr, json_key->private_key) !=
      1) {
#endif
    LOG(ERROR) << "DigestInit failed.";
    goto end;
  }
  if (EVP_DigestSignUpdate(md_ctx, to_sign, strlen(to_sign)) != 1) {
    LOG(ERROR) << "DigestUpdate failed.";
    goto end;
  }
  if (EVP_DigestSignFinal(md_ctx, nullptr, &sig_len) != 1) {
    LOG(ERROR) << "DigestFinal (get signature length) failed.";
    goto end;
  }
  sig = static_cast<unsigned char*>(gpr_malloc(sig_len));
  if (EVP_DigestSignFinal(md_ctx, sig, &sig_len) != 1) {
    LOG(ERROR) << "DigestFinal (signature compute) failed.";
    goto end;
  }
  result =
      gpr_strdup(absl::WebSafeBase64Escape(
                     absl::string_view(reinterpret_cast<char*>(sig), sig_len))
                     .c_str());

end:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  if (key != nullptr) EVP_PKEY_free(key);
#endif
  if (md_ctx != nullptr) EVP_MD_CTX_destroy(md_ctx);
  if (sig != nullptr) gpr_free(sig);
  return result;
}

char* grpc_jwt_encode_and_sign(const grpc_auth_json_key* json_key,
                               const char* audience,
                               gpr_timespec token_lifetime, const char* scope) {
  if (g_jwt_encode_and_sign_override != nullptr) {
    return g_jwt_encode_and_sign_override(json_key, audience, token_lifetime,
                                          scope);
  } else {
    const char* sig_algo = GRPC_JWT_RSA_SHA256_ALGORITHM;
    char* to_sign = dot_concat_and_free_strings(
        encoded_jwt_header(json_key->private_key_id, sig_algo),
        encoded_jwt_claim(json_key, audience, token_lifetime, scope));
    char* sig = compute_and_encode_signature(json_key, sig_algo, to_sign);
    if (sig == nullptr) {
      gpr_free(to_sign);
      return nullptr;
    }
    return dot_concat_and_free_strings(to_sign, sig);
  }
}

void grpc_jwt_encode_and_sign_set_override(
    grpc_jwt_encode_and_sign_override func) {
  g_jwt_encode_and_sign_override = func;
}
