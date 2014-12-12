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
#include <grpc/support/string.h>
#include <openssl/bio.h>
#include <openssl/pem.h>

#include "third_party/cJSON/cJSON.h"

/* --- Constants. --- */

/* 1 hour max. */
const gpr_timespec grpc_max_service_accounts_token_validity = {3600, 0};

#define GRPC_AUTH_JSON_KEY_TYPE_INVALID "invalid"
#define GRPC_AUTH_JSON_KEY_TYPE_SERVICE_ACCOUNT "service_account"

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

int grpc_auth_json_key_is_valid(grpc_auth_json_key *json_key) {
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
