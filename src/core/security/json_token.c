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

#include "src/core/security/json_token.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/security/base64.h"
#include "src/core/support/string.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include "third_party/cJSON/cJSON.h"

/* --- Constants. --- */

/* 1 hour max. */
const gpr_timespec grpc_max_auth_token_lifetime = {3600, 0};

#define GRPC_AUTH_JSON_KEY_TYPE_INVALID "invalid"
#define GRPC_AUTH_JSON_KEY_TYPE_SERVICE_ACCOUNT "service_account"

#define GRPC_JWT_AUDIENCE "https://www.googleapis.com/oauth2/v3/token"
#define GRPC_JWT_RSA_SHA256_ALGORITHM "RS256"
#define GRPC_JWT_TYPE "JWT"

/* --- Override for testing. --- */

static grpc_jwt_encode_and_sign_override g_jwt_encode_and_sign_override = NULL;

/* --- grpc_auth_json_key. --- */

static const char *json_get_string_property(cJSON *json,
                                            const char *prop_name) {
  cJSON *child = NULL;
  child = cJSON_GetObjectItem(json, prop_name);
  if (child == NULL || child->type != cJSON_String) {
    gpr_log(GPR_ERROR, "Invalid or missing %s property.", prop_name);
    return NULL;
  }
  return child->valuestring;
}

static int set_json_key_string_property(cJSON *json, const char *prop_name,
                                        char **json_key_field) {
  const char *prop_value = json_get_string_property(json, prop_name);
  if (prop_value == NULL) return 0;
  *json_key_field = gpr_strdup(prop_value);
  return 1;
}

int grpc_auth_json_key_is_valid(const grpc_auth_json_key *json_key) {
  return (json_key != NULL) &&
         strcmp(json_key->type, GRPC_AUTH_JSON_KEY_TYPE_INVALID);
}

grpc_auth_json_key grpc_auth_json_key_create_from_string(
    const char *json_string) {
  grpc_auth_json_key result;
  cJSON *json = cJSON_Parse(json_string);
  BIO *bio = NULL;
  const char *prop_value;
  int success = 0;

  memset(&result, 0, sizeof(grpc_auth_json_key));
  result.type = GRPC_AUTH_JSON_KEY_TYPE_INVALID;
  if (json == NULL) {
    gpr_log(GPR_ERROR, "Invalid json string %s", json_string);
    return result;
  }

  prop_value = json_get_string_property(json, "type");
  if (prop_value == NULL ||
      strcmp(prop_value, GRPC_AUTH_JSON_KEY_TYPE_SERVICE_ACCOUNT)) {
    goto end;
  }
  result.type = GRPC_AUTH_JSON_KEY_TYPE_SERVICE_ACCOUNT;

  if (!set_json_key_string_property(json, "private_key_id",
                                    &result.private_key_id) ||
      !set_json_key_string_property(json, "client_id", &result.client_id) ||
      !set_json_key_string_property(json, "client_email",
                                    &result.client_email)) {
    goto end;
  }

  prop_value = json_get_string_property(json, "private_key");
  if (prop_value == NULL) {
    goto end;
  }
  bio = BIO_new(BIO_s_mem());
  if (BIO_puts(bio, prop_value) != strlen(prop_value)) {
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
  if (json != NULL) cJSON_Delete(json);
  if (!success) grpc_auth_json_key_destruct(&result);
  return result;
}

void grpc_auth_json_key_destruct(grpc_auth_json_key *json_key) {
  if (json_key == NULL) return;
  json_key->type = GRPC_AUTH_JSON_KEY_TYPE_INVALID;
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

static char *encoded_jwt_header(const char *algorithm) {
  cJSON *json = cJSON_CreateObject();
  cJSON *child = cJSON_CreateString(algorithm);
  char *json_str = NULL;
  char *result = NULL;
  cJSON_AddItemToObject(json, "alg", child);
  child = cJSON_CreateString(GRPC_JWT_TYPE);
  cJSON_AddItemToObject(json, "typ", child);
  json_str = cJSON_PrintUnformatted(json);
  result = grpc_base64_encode(json_str, strlen(json_str), 1, 0);
  free(json_str);
  cJSON_Delete(json);
  return result;
}

static char *encoded_jwt_claim(const grpc_auth_json_key *json_key,
                               const char *scope, gpr_timespec token_lifetime) {
  cJSON *json = cJSON_CreateObject();
  cJSON *child = NULL;
  char *json_str = NULL;
  char *result = NULL;
  gpr_timespec now = gpr_now();
  gpr_timespec expiration = gpr_time_add(now, token_lifetime);
  if (gpr_time_cmp(token_lifetime, grpc_max_auth_token_lifetime) > 0) {
    gpr_log(GPR_INFO, "Cropping token lifetime to maximum allowed value.");
    expiration = gpr_time_add(now, grpc_max_auth_token_lifetime);
  }
  child = cJSON_CreateString(json_key->client_email);
  cJSON_AddItemToObject(json, "iss", child);
  child = cJSON_CreateString(scope);
  cJSON_AddItemToObject(json, "scope", child);
  child = cJSON_CreateString(GRPC_JWT_AUDIENCE);
  cJSON_AddItemToObject(json, "aud", child);
  child = cJSON_CreateNumber(now.tv_sec);
  cJSON_SetIntValue(child, now.tv_sec);
  cJSON_AddItemToObject(json, "iat", child);
  child = cJSON_CreateNumber(expiration.tv_sec);
  cJSON_SetIntValue(child, expiration.tv_sec);
  cJSON_AddItemToObject(json, "exp", child);
  json_str = cJSON_PrintUnformatted(json);
  result = grpc_base64_encode(json_str, strlen(json_str), 1, 0);
  free(json_str);
  cJSON_Delete(json);
  return result;
}

static char *dot_concat_and_free_strings(char *str1, char *str2) {
  size_t str1_len = strlen(str1);
  size_t str2_len = strlen(str2);
  size_t result_len = str1_len + 1 /* dot */ + str2_len;
  char *result = gpr_malloc(result_len + 1 /* NULL terminated */);
  char *current = result;
  strncpy(current, str1, str1_len);
  current += str1_len;
  *(current++) = '.';
  strncpy(current, str2, str2_len);
  current += str2_len;
  GPR_ASSERT((current - result) == result_len);
  *current = '\0';
  gpr_free(str1);
  gpr_free(str2);
  return result;
}

const EVP_MD *openssl_digest_from_algorithm(const char *algorithm) {
  if (!strcmp(algorithm, GRPC_JWT_RSA_SHA256_ALGORITHM)) {
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
                               const char *scope, gpr_timespec token_lifetime) {
  if (g_jwt_encode_and_sign_override != NULL) {
    return g_jwt_encode_and_sign_override(json_key, scope, token_lifetime);
  } else {
    const char *sig_algo = GRPC_JWT_RSA_SHA256_ALGORITHM;
    char *to_sign = dot_concat_and_free_strings(
        encoded_jwt_header(sig_algo),
        encoded_jwt_claim(json_key, scope, token_lifetime));
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
