/*
 *
 * Copyright 2015, Google Inc.
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

#include "src/core/lib/security/credentials/jwt/json_token.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/security/util/b64.h"
#include "src/core/lib/security/util/json_util.h"
#include "src/core/lib/support/string.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

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

static grpc_jwt_encode_and_sign_override g_jwt_encode_and_sign_override = NULL;

/* --- grpc_auth_json_key. --- */

int grpc_auth_json_key_is_valid(const grpc_auth_json_key *json_key) {
  return (json_key != NULL) &&
         strcmp(json_key->type, GRPC_AUTH_JSON_TYPE_INVALID);
}

grpc_auth_json_key grpc_auth_json_key_create_from_json(const grpc_json *json) {
  grpc_auth_json_key result;
  BIO *bio = NULL;
  const char *prop_value;
  int success = 0;

  memset(&result, 0, sizeof(grpc_auth_json_key));
  result.type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (json == NULL) {
    gpr_log(GPR_ERROR, "Invalid json.");
    goto end;
  }

  prop_value = grpc_json_get_string_property(json, "type");
  if (prop_value == NULL ||
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
  if (prop_value == NULL) {
    goto end;
  }
  bio = BIO_new(BIO_s_mem());
  success = BIO_puts(bio, prop_value);
  if ((success < 0) || ((size_t)success != strlen(prop_value))) {
    gpr_log(GPR_ERROR, "Could not write into openssl BIO.");
    goto end;
  }
  result.private_key = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, "");
  if (result.private_key == NULL) {
    gpr_log(GPR_ERROR, "Could not deserialize private key.");
    goto end;
  }
  success = 1;

end:
  if (bio != NULL) BIO_free(bio);
  if (!success) grpc_auth_json_key_destruct(&result);
  return result;
}

grpc_auth_json_key grpc_auth_json_key_create_from_string(
    const char *json_string) {
  char *scratchpad = gpr_strdup(json_string);
  grpc_json *json = grpc_json_parse_string(scratchpad);
  grpc_auth_json_key result = grpc_auth_json_key_create_from_json(json);
  if (json != NULL) grpc_json_destroy(json);
  gpr_free(scratchpad);
  return result;
}

void grpc_auth_json_key_destruct(grpc_auth_json_key *json_key) {
  if (json_key == NULL) return;
  json_key->type = GRPC_AUTH_JSON_TYPE_INVALID;
  if (json_key->client_id != NULL) {
    gpr_free(json_key->client_id);
    json_key->client_id = NULL;
  }
  if (json_key->private_key_id != NULL) {
    gpr_free(json_key->private_key_id);
    json_key->private_key_id = NULL;
  }
  if (json_key->client_email != NULL) {
    gpr_free(json_key->client_email);
    json_key->client_email = NULL;
  }
  if (json_key->private_key != NULL) {
    RSA_free(json_key->private_key);
    json_key->private_key = NULL;
  }
}

/* --- jwt encoding and signature. --- */

static grpc_json *create_child(grpc_json *brother, grpc_json *parent,
                               const char *key, const char *value,
                               grpc_json_type type) {
  grpc_json *child = grpc_json_create(type);
  if (brother) brother->next = child;
  if (!parent->child) parent->child = child;
  child->parent = parent;
  child->value = value;
  child->key = key;
  return child;
}

static char *encoded_jwt_header(const char *key_id, const char *algorithm) {
  grpc_json *json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json *child = NULL;
  char *json_str = NULL;
  char *result = NULL;

  child = create_child(NULL, json, "alg", algorithm, GRPC_JSON_STRING);
  child = create_child(child, json, "typ", GRPC_JWT_TYPE, GRPC_JSON_STRING);
  create_child(child, json, "kid", key_id, GRPC_JSON_STRING);

  json_str = grpc_json_dump_to_string(json, 0);
  result = grpc_base64_encode(json_str, strlen(json_str), 1, 0);
  gpr_free(json_str);
  grpc_json_destroy(json);
  return result;
}

static char *encoded_jwt_claim(const grpc_auth_json_key *json_key,
                               const char *audience,
                               gpr_timespec token_lifetime, const char *scope) {
  grpc_json *json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json *child = NULL;
  char *json_str = NULL;
  char *result = NULL;
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

  child =
      create_child(NULL, json, "iss", json_key->client_email, GRPC_JSON_STRING);
  if (scope != NULL) {
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

static char *dot_concat_and_free_strings(char *str1, char *str2) {
  size_t str1_len = strlen(str1);
  size_t str2_len = strlen(str2);
  size_t result_len = str1_len + 1 /* dot */ + str2_len;
  char *result = gpr_malloc(result_len + 1 /* NULL terminated */);
  char *current = result;
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

const EVP_MD *openssl_digest_from_algorithm(const char *algorithm) {
  if (strcmp(algorithm, GRPC_JWT_RSA_SHA256_ALGORITHM) == 0) {
    return EVP_sha256();
  } else {
    gpr_log(GPR_ERROR, "Unknown algorithm %s.", algorithm);
    return NULL;
  }
}

char *compute_and_encode_signature(const grpc_auth_json_key *json_key,
                                   const char *signature_algorithm,
                                   const char *to_sign) {
  const EVP_MD *md = openssl_digest_from_algorithm(signature_algorithm);
  EVP_MD_CTX *md_ctx = NULL;
  EVP_PKEY *key = EVP_PKEY_new();
  size_t sig_len = 0;
  unsigned char *sig = NULL;
  char *result = NULL;
  if (md == NULL) return NULL;
  md_ctx = EVP_MD_CTX_create();
  if (md_ctx == NULL) {
    gpr_log(GPR_ERROR, "Could not create MD_CTX");
    goto end;
  }
  EVP_PKEY_set1_RSA(key, json_key->private_key);
  if (EVP_DigestSignInit(md_ctx, NULL, md, NULL, key) != 1) {
    gpr_log(GPR_ERROR, "DigestInit failed.");
    goto end;
  }
  if (EVP_DigestSignUpdate(md_ctx, to_sign, strlen(to_sign)) != 1) {
    gpr_log(GPR_ERROR, "DigestUpdate failed.");
    goto end;
  }
  if (EVP_DigestSignFinal(md_ctx, NULL, &sig_len) != 1) {
    gpr_log(GPR_ERROR, "DigestFinal (get signature length) failed.");
    goto end;
  }
  sig = gpr_malloc(sig_len);
  if (EVP_DigestSignFinal(md_ctx, sig, &sig_len) != 1) {
    gpr_log(GPR_ERROR, "DigestFinal (signature compute) failed.");
    goto end;
  }
  result = grpc_base64_encode(sig, sig_len, 1, 0);

end:
  if (key != NULL) EVP_PKEY_free(key);
  if (md_ctx != NULL) EVP_MD_CTX_destroy(md_ctx);
  if (sig != NULL) gpr_free(sig);
  return result;
}

char *grpc_jwt_encode_and_sign(const grpc_auth_json_key *json_key,
                               const char *audience,
                               gpr_timespec token_lifetime, const char *scope) {
  if (g_jwt_encode_and_sign_override != NULL) {
    return g_jwt_encode_and_sign_override(json_key, audience, token_lifetime,
                                          scope);
  } else {
    const char *sig_algo = GRPC_JWT_RSA_SHA256_ALGORITHM;
    char *to_sign = dot_concat_and_free_strings(
        encoded_jwt_header(json_key->private_key_id, sig_algo),
        encoded_jwt_claim(json_key, audience, token_lifetime, scope));
    char *sig = compute_and_encode_signature(json_key, sig_algo, to_sign);
    if (sig == NULL) {
      gpr_free(to_sign);
      return NULL;
    }
    return dot_concat_and_free_strings(to_sign, sig);
  }
}

void grpc_jwt_encode_and_sign_set_override(
    grpc_jwt_encode_and_sign_override func) {
  g_jwt_encode_and_sign_override = func;
}
