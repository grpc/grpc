/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/security/credentials/jwt/json_token.h"

#include <string.h>

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/security/util/json_util.h"
#include "src/core/lib/slice/b64.h"

extern "C" {
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
}

/* --- Constants. --- */

/* 1 hour max. */
gpr_timespec grpc_max_auth_token_lifetime() {
  gpr_timespec out;
  out.tv_sec = 3600;
  out.tv_nsec = 0;
  out.clock_type = GPR_TIMESPAN;
  return out;
}

#define GRPC_JWT_RSA_SHA256_ALGORITHM "RS256"
#define GRPC_JWT_TYPE "JWT"

/* --- Override for testing. --- */

static grpc_jwt_encode_and_sign_override g_jwt_encode_and_sign_override =
    nullptr;

/* --- grpc_auth_json_key. --- */

int grpc_auth_json_key_is_valid(const grpc_auth_json_key* json_key) {
  return (json_key != nullptr) &&
         strcmp(json_key->type, GRPC_AUTH_JSON_TYPE_INVALID);
}

grpc_auth_json_key grpc_auth_json_key_create_from_json(const grpc_json* json) {
  grpc_auth_json_key result;
  BIO* bio = nullptr;
  const char* prop_value;
  int success = 0;

  memset(&result, 0, sizeof(grpc_auth_json_key));
  result.type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (json == nullptr) {
    gpr_log(GPR_ERROR, "Invalid json.");
    goto end;
  }

  prop_value = grpc_json_get_string_property(json, "type");
  if (prop_value == nullptr ||
      strcmp(prop_value, GRPC_AUTH_JSON_TYPE_SERVICE_ACCOUNT)) {
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

  prop_value = grpc_json_get_string_property(json, "private_key");
  if (prop_value == nullptr) {
    goto end;
  }
  bio = BIO_new(BIO_s_mem());
  success = BIO_puts(bio, prop_value);
  if ((success < 0) || (static_cast<size_t>(success) != strlen(prop_value))) {
    gpr_log(GPR_ERROR, "Could not write into openssl BIO.");
    goto end;
  }
  result.private_key =
      PEM_read_bio_RSAPrivateKey(bio, nullptr, nullptr, (void*)"");
  if (result.private_key == nullptr) {
    gpr_log(GPR_ERROR, "Could not deserialize private key.");
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
  char* scratchpad = gpr_strdup(json_string);
  grpc_json* json = grpc_json_parse_string(scratchpad);
  grpc_auth_json_key result = grpc_auth_json_key_create_from_json(json);
  if (json != nullptr) grpc_json_destroy(json);
  gpr_free(scratchpad);
  return result;
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
    RSA_free(json_key->private_key);
    json_key->private_key = nullptr;
  }
}

/* --- jwt encoding and signature. --- */

static grpc_json* create_child(grpc_json* brother, grpc_json* parent,
                               const char* key, const char* value,
                               grpc_json_type type) {
  grpc_json* child = grpc_json_create(type);
  if (brother) brother->next = child;
  if (!parent->child) parent->child = child;
  child->parent = parent;
  child->value = value;
  child->key = key;
  return child;
}

static char* encoded_jwt_header(const char* key_id, const char* algorithm) {
  grpc_json* json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* child = nullptr;
  char* json_str = nullptr;
  char* result = nullptr;

  child = create_child(nullptr, json, "alg", algorithm, GRPC_JSON_STRING);
  child = create_child(child, json, "typ", GRPC_JWT_TYPE, GRPC_JSON_STRING);
  create_child(child, json, "kid", key_id, GRPC_JSON_STRING);

  json_str = grpc_json_dump_to_string(json, 0);
  result = grpc_base64_encode(json_str, strlen(json_str), 1, 0);
  gpr_free(json_str);
  grpc_json_destroy(json);
  return result;
}

static char* encoded_jwt_claim(const grpc_auth_json_key* json_key,
                               const char* audience,
                               gpr_timespec token_lifetime, const char* scope) {
  grpc_json* json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* child = nullptr;
  char* json_str = nullptr;
  char* result = nullptr;
  gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
  gpr_timespec expiration = gpr_time_add(now, token_lifetime);
  char now_str[GPR_LTOA_MIN_BUFSIZE];
  char expiration_str[GPR_LTOA_MIN_BUFSIZE];
  if (gpr_time_cmp(token_lifetime, grpc_max_auth_token_lifetime()) > 0) {
    gpr_log(GPR_INFO, "Cropping token lifetime to maximum allowed value.");
    expiration = gpr_time_add(now, grpc_max_auth_token_lifetime());
  }
  int64_ttoa(now.tv_sec, now_str);
  int64_ttoa(expiration.tv_sec, expiration_str);

  child = create_child(nullptr, json, "iss", json_key->client_email,
                       GRPC_JSON_STRING);
  if (scope != nullptr) {
    child = create_child(child, json, "scope", scope, GRPC_JSON_STRING);
  } else {
    /* Unscoped JWTs need a sub field. */
    child = create_child(child, json, "sub", json_key->client_email,
                         GRPC_JSON_STRING);
  }

  child = create_child(child, json, "aud", audience, GRPC_JSON_STRING);
  child = create_child(child, json, "iat", now_str, GRPC_JSON_NUMBER);
  create_child(child, json, "exp", expiration_str, GRPC_JSON_NUMBER);

  json_str = grpc_json_dump_to_string(json, 0);
  result = grpc_base64_encode(json_str, strlen(json_str), 1, 0);
  gpr_free(json_str);
  grpc_json_destroy(json);
  return result;
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
  GPR_ASSERT(current >= result);
  GPR_ASSERT((uintptr_t)(current - result) == result_len);
  *current = '\0';
  gpr_free(str1);
  gpr_free(str2);
  return result;
}

const EVP_MD* openssl_digest_from_algorithm(const char* algorithm) {
  if (strcmp(algorithm, GRPC_JWT_RSA_SHA256_ALGORITHM) == 0) {
    return EVP_sha256();
  } else {
    gpr_log(GPR_ERROR, "Unknown algorithm %s.", algorithm);
    return nullptr;
  }
}

char* compute_and_encode_signature(const grpc_auth_json_key* json_key,
                                   const char* signature_algorithm,
                                   const char* to_sign) {
  const EVP_MD* md = openssl_digest_from_algorithm(signature_algorithm);
  EVP_MD_CTX* md_ctx = nullptr;
  EVP_PKEY* key = EVP_PKEY_new();
  size_t sig_len = 0;
  unsigned char* sig = nullptr;
  char* result = nullptr;
  if (md == nullptr) return nullptr;
  md_ctx = EVP_MD_CTX_create();
  if (md_ctx == nullptr) {
    gpr_log(GPR_ERROR, "Could not create MD_CTX");
    goto end;
  }
  EVP_PKEY_set1_RSA(key, json_key->private_key);
  if (EVP_DigestSignInit(md_ctx, nullptr, md, nullptr, key) != 1) {
    gpr_log(GPR_ERROR, "DigestInit failed.");
    goto end;
  }
  if (EVP_DigestSignUpdate(md_ctx, to_sign, strlen(to_sign)) != 1) {
    gpr_log(GPR_ERROR, "DigestUpdate failed.");
    goto end;
  }
  if (EVP_DigestSignFinal(md_ctx, nullptr, &sig_len) != 1) {
    gpr_log(GPR_ERROR, "DigestFinal (get signature length) failed.");
    goto end;
  }
  sig = static_cast<unsigned char*>(gpr_malloc(sig_len));
  if (EVP_DigestSignFinal(md_ctx, sig, &sig_len) != 1) {
    gpr_log(GPR_ERROR, "DigestFinal (signature compute) failed.");
    goto end;
  }
  result = grpc_base64_encode(sig, sig_len, 1, 0);

end:
  if (key != nullptr) EVP_PKEY_free(key);
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
