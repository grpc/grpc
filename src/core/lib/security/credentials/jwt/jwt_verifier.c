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

#include "src/core/lib/security/credentials/jwt/jwt_verifier.h"

#include <limits.h>
#include <string.h>

#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/security/util/b64.h"
#include "src/core/lib/tsi/ssl_types.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>
#include <openssl/pem.h>

/* --- Utils. --- */

const char *grpc_jwt_verifier_status_to_string(
    grpc_jwt_verifier_status status) {
  switch (status) {
    case GRPC_JWT_VERIFIER_OK:
      return "OK";
    case GRPC_JWT_VERIFIER_BAD_SIGNATURE:
      return "BAD_SIGNATURE";
    case GRPC_JWT_VERIFIER_BAD_FORMAT:
      return "BAD_FORMAT";
    case GRPC_JWT_VERIFIER_BAD_AUDIENCE:
      return "BAD_AUDIENCE";
    case GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR:
      return "KEY_RETRIEVAL_ERROR";
    case GRPC_JWT_VERIFIER_TIME_CONSTRAINT_FAILURE:
      return "TIME_CONSTRAINT_FAILURE";
    case GRPC_JWT_VERIFIER_GENERIC_ERROR:
      return "GENERIC_ERROR";
    default:
      return "UNKNOWN";
  }
}

static const EVP_MD *evp_md_from_alg(const char *alg) {
  if (strcmp(alg, "RS256") == 0) {
    return EVP_sha256();
  } else if (strcmp(alg, "RS384") == 0) {
    return EVP_sha384();
  } else if (strcmp(alg, "RS512") == 0) {
    return EVP_sha512();
  } else {
    return NULL;
  }
}

static grpc_json *parse_json_part_from_jwt(const char *str, size_t len,
                                           gpr_slice *buffer) {
  grpc_json *json;

  *buffer = grpc_base64_decode_with_len(str, len, 1);
  if (GPR_SLICE_IS_EMPTY(*buffer)) {
    gpr_log(GPR_ERROR, "Invalid base64.");
    return NULL;
  }
  json = grpc_json_parse_string_with_len((char *)GPR_SLICE_START_PTR(*buffer),
                                         GPR_SLICE_LENGTH(*buffer));
  if (json == NULL) {
    gpr_slice_unref(*buffer);
    gpr_log(GPR_ERROR, "JSON parsing error.");
  }
  return json;
}

static const char *validate_string_field(const grpc_json *json,
                                         const char *key) {
  if (json->type != GRPC_JSON_STRING) {
    gpr_log(GPR_ERROR, "Invalid %s field [%s]", key, json->value);
    return NULL;
  }
  return json->value;
}

static gpr_timespec validate_time_field(const grpc_json *json,
                                        const char *key) {
  gpr_timespec result = gpr_time_0(GPR_CLOCK_REALTIME);
  if (json->type != GRPC_JSON_NUMBER) {
    gpr_log(GPR_ERROR, "Invalid %s field [%s]", key, json->value);
    return result;
  }
  result.tv_sec = strtol(json->value, NULL, 10);
  return result;
}

/* --- JOSE header. see http://tools.ietf.org/html/rfc7515#section-4 --- */

typedef struct {
  const char *alg;
  const char *kid;
  const char *typ;
  /* TODO(jboeuf): Add others as needed (jku, jwk, x5u, x5c and so on...). */
  gpr_slice buffer;
} jose_header;

static void jose_header_destroy(jose_header *h) {
  gpr_slice_unref(h->buffer);
  gpr_free(h);
}

/* Takes ownership of json and buffer. */
static jose_header *jose_header_from_json(grpc_json *json, gpr_slice buffer) {
  grpc_json *cur;
  jose_header *h = gpr_malloc(sizeof(jose_header));
  memset(h, 0, sizeof(jose_header));
  h->buffer = buffer;
  for (cur = json->child; cur != NULL; cur = cur->next) {
    if (strcmp(cur->key, "alg") == 0) {
      /* We only support RSA-1.5 signatures for now.
         Beware of this if we add HMAC support:
         https://auth0.com/blog/2015/03/31/critical-vulnerabilities-in-json-web-token-libraries/
       */
      if (cur->type != GRPC_JSON_STRING || strncmp(cur->value, "RS", 2) ||
          evp_md_from_alg(cur->value) == NULL) {
        gpr_log(GPR_ERROR, "Invalid alg field [%s]", cur->value);
        goto error;
      }
      h->alg = cur->value;
    } else if (strcmp(cur->key, "typ") == 0) {
      h->typ = validate_string_field(cur, "typ");
      if (h->typ == NULL) goto error;
    } else if (strcmp(cur->key, "kid") == 0) {
      h->kid = validate_string_field(cur, "kid");
      if (h->kid == NULL) goto error;
    }
  }
  if (h->alg == NULL) {
    gpr_log(GPR_ERROR, "Missing alg field.");
    goto error;
  }
  grpc_json_destroy(json);
  h->buffer = buffer;
  return h;

error:
  grpc_json_destroy(json);
  jose_header_destroy(h);
  return NULL;
}

/* --- JWT claims. see http://tools.ietf.org/html/rfc7519#section-4.1 */

struct grpc_jwt_claims {
  /* Well known properties already parsed. */
  const char *sub;
  const char *iss;
  const char *aud;
  const char *jti;
  gpr_timespec iat;
  gpr_timespec exp;
  gpr_timespec nbf;

  grpc_json *json;
  gpr_slice buffer;
};

void grpc_jwt_claims_destroy(grpc_jwt_claims *claims) {
  grpc_json_destroy(claims->json);
  gpr_slice_unref(claims->buffer);
  gpr_free(claims);
}

const grpc_json *grpc_jwt_claims_json(const grpc_jwt_claims *claims) {
  if (claims == NULL) return NULL;
  return claims->json;
}

const char *grpc_jwt_claims_subject(const grpc_jwt_claims *claims) {
  if (claims == NULL) return NULL;
  return claims->sub;
}

const char *grpc_jwt_claims_issuer(const grpc_jwt_claims *claims) {
  if (claims == NULL) return NULL;
  return claims->iss;
}

const char *grpc_jwt_claims_id(const grpc_jwt_claims *claims) {
  if (claims == NULL) return NULL;
  return claims->jti;
}

const char *grpc_jwt_claims_audience(const grpc_jwt_claims *claims) {
  if (claims == NULL) return NULL;
  return claims->aud;
}

gpr_timespec grpc_jwt_claims_issued_at(const grpc_jwt_claims *claims) {
  if (claims == NULL) return gpr_inf_past(GPR_CLOCK_REALTIME);
  return claims->iat;
}

gpr_timespec grpc_jwt_claims_expires_at(const grpc_jwt_claims *claims) {
  if (claims == NULL) return gpr_inf_future(GPR_CLOCK_REALTIME);
  return claims->exp;
}

gpr_timespec grpc_jwt_claims_not_before(const grpc_jwt_claims *claims) {
  if (claims == NULL) return gpr_inf_past(GPR_CLOCK_REALTIME);
  return claims->nbf;
}

/* Takes ownership of json and buffer even in case of failure. */
grpc_jwt_claims *grpc_jwt_claims_from_json(grpc_json *json, gpr_slice buffer) {
  grpc_json *cur;
  grpc_jwt_claims *claims = gpr_malloc(sizeof(grpc_jwt_claims));
  memset(claims, 0, sizeof(grpc_jwt_claims));
  claims->json = json;
  claims->buffer = buffer;
  claims->iat = gpr_inf_past(GPR_CLOCK_REALTIME);
  claims->nbf = gpr_inf_past(GPR_CLOCK_REALTIME);
  claims->exp = gpr_inf_future(GPR_CLOCK_REALTIME);

  /* Per the spec, all fields are optional. */
  for (cur = json->child; cur != NULL; cur = cur->next) {
    if (strcmp(cur->key, "sub") == 0) {
      claims->sub = validate_string_field(cur, "sub");
      if (claims->sub == NULL) goto error;
    } else if (strcmp(cur->key, "iss") == 0) {
      claims->iss = validate_string_field(cur, "iss");
      if (claims->iss == NULL) goto error;
    } else if (strcmp(cur->key, "aud") == 0) {
      claims->aud = validate_string_field(cur, "aud");
      if (claims->aud == NULL) goto error;
    } else if (strcmp(cur->key, "jti") == 0) {
      claims->jti = validate_string_field(cur, "jti");
      if (claims->jti == NULL) goto error;
    } else if (strcmp(cur->key, "iat") == 0) {
      claims->iat = validate_time_field(cur, "iat");
      if (gpr_time_cmp(claims->iat, gpr_time_0(GPR_CLOCK_REALTIME)) == 0)
        goto error;
    } else if (strcmp(cur->key, "exp") == 0) {
      claims->exp = validate_time_field(cur, "exp");
      if (gpr_time_cmp(claims->exp, gpr_time_0(GPR_CLOCK_REALTIME)) == 0)
        goto error;
    } else if (strcmp(cur->key, "nbf") == 0) {
      claims->nbf = validate_time_field(cur, "nbf");
      if (gpr_time_cmp(claims->nbf, gpr_time_0(GPR_CLOCK_REALTIME)) == 0)
        goto error;
    }
  }
  return claims;

error:
  grpc_jwt_claims_destroy(claims);
  return NULL;
}

grpc_jwt_verifier_status grpc_jwt_claims_check(const grpc_jwt_claims *claims,
                                               const char *audience) {
  gpr_timespec skewed_now;
  int audience_ok;

  GPR_ASSERT(claims != NULL);

  skewed_now =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), grpc_jwt_verifier_clock_skew);
  if (gpr_time_cmp(skewed_now, claims->nbf) < 0) {
    gpr_log(GPR_ERROR, "JWT is not valid yet.");
    return GRPC_JWT_VERIFIER_TIME_CONSTRAINT_FAILURE;
  }
  skewed_now =
      gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), grpc_jwt_verifier_clock_skew);
  if (gpr_time_cmp(skewed_now, claims->exp) > 0) {
    gpr_log(GPR_ERROR, "JWT is expired.");
    return GRPC_JWT_VERIFIER_TIME_CONSTRAINT_FAILURE;
  }

  if (audience == NULL) {
    audience_ok = claims->aud == NULL;
  } else {
    audience_ok = claims->aud != NULL && strcmp(audience, claims->aud) == 0;
  }
  if (!audience_ok) {
    gpr_log(GPR_ERROR, "Audience mismatch: expected %s and found %s.",
            audience == NULL ? "NULL" : audience,
            claims->aud == NULL ? "NULL" : claims->aud);
    return GRPC_JWT_VERIFIER_BAD_AUDIENCE;
  }
  return GRPC_JWT_VERIFIER_OK;
}

/* --- verifier_cb_ctx object. --- */

typedef enum {
  HTTP_RESPONSE_OPENID = 0,
  HTTP_RESPONSE_KEYS,
  HTTP_RESPONSE_COUNT /* must be last */
} http_response_index;

typedef struct {
  grpc_jwt_verifier *verifier;
  grpc_polling_entity pollent;
  jose_header *header;
  grpc_jwt_claims *claims;
  char *audience;
  gpr_slice signature;
  gpr_slice signed_data;
  void *user_data;
  grpc_jwt_verification_done_cb user_cb;
  grpc_http_response responses[HTTP_RESPONSE_COUNT];
} verifier_cb_ctx;

/* Takes ownership of the header, claims and signature. */
static verifier_cb_ctx *verifier_cb_ctx_create(
    grpc_jwt_verifier *verifier, grpc_pollset *pollset, jose_header *header,
    grpc_jwt_claims *claims, const char *audience, gpr_slice signature,
    const char *signed_jwt, size_t signed_jwt_len, void *user_data,
    grpc_jwt_verification_done_cb cb) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  verifier_cb_ctx *ctx = gpr_malloc(sizeof(verifier_cb_ctx));
  memset(ctx, 0, sizeof(verifier_cb_ctx));
  ctx->verifier = verifier;
  ctx->pollent = grpc_polling_entity_create_from_pollset(pollset);
  ctx->header = header;
  ctx->audience = gpr_strdup(audience);
  ctx->claims = claims;
  ctx->signature = signature;
  ctx->signed_data = gpr_slice_from_copied_buffer(signed_jwt, signed_jwt_len);
  ctx->user_data = user_data;
  ctx->user_cb = cb;
  grpc_exec_ctx_finish(&exec_ctx);
  return ctx;
}

void verifier_cb_ctx_destroy(verifier_cb_ctx *ctx) {
  if (ctx->audience != NULL) gpr_free(ctx->audience);
  if (ctx->claims != NULL) grpc_jwt_claims_destroy(ctx->claims);
  gpr_slice_unref(ctx->signature);
  gpr_slice_unref(ctx->signed_data);
  jose_header_destroy(ctx->header);
  for (size_t i = 0; i < HTTP_RESPONSE_COUNT; i++) {
    grpc_http_response_destroy(&ctx->responses[i]);
  }
  /* TODO: see what to do with claims... */
  gpr_free(ctx);
}

/* --- grpc_jwt_verifier object. --- */

/* Clock skew defaults to one minute. */
gpr_timespec grpc_jwt_verifier_clock_skew = {60, 0, GPR_TIMESPAN};

/* Max delay defaults to one minute. */
gpr_timespec grpc_jwt_verifier_max_delay = {60, 0, GPR_TIMESPAN};

typedef struct {
  char *email_domain;
  char *key_url_prefix;
} email_key_mapping;

struct grpc_jwt_verifier {
  email_key_mapping *mappings;
  size_t num_mappings; /* Should be very few, linear search ok. */
  size_t allocated_mappings;
  grpc_httpcli_context http_ctx;
};

static grpc_json *json_from_http(const grpc_httpcli_response *response) {
  grpc_json *json = NULL;

  if (response == NULL) {
    gpr_log(GPR_ERROR, "HTTP response is NULL.");
    return NULL;
  }
  if (response->status != 200) {
    gpr_log(GPR_ERROR, "Call to http server failed with error %d.",
            response->status);
    return NULL;
  }

  json = grpc_json_parse_string_with_len(response->body, response->body_length);
  if (json == NULL) {
    gpr_log(GPR_ERROR, "Invalid JSON found in response.");
  }
  return json;
}

static const grpc_json *find_property_by_name(const grpc_json *json,
                                              const char *name) {
  const grpc_json *cur;
  for (cur = json->child; cur != NULL; cur = cur->next) {
    if (strcmp(cur->key, name) == 0) return cur;
  }
  return NULL;
}

static EVP_PKEY *extract_pkey_from_x509(const char *x509_str) {
  X509 *x509 = NULL;
  EVP_PKEY *result = NULL;
  BIO *bio = BIO_new(BIO_s_mem());
  size_t len = strlen(x509_str);
  GPR_ASSERT(len < INT_MAX);
  BIO_write(bio, x509_str, (int)len);
  x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL);
  if (x509 == NULL) {
    gpr_log(GPR_ERROR, "Unable to parse x509 cert.");
    goto end;
  }
  result = X509_get_pubkey(x509);
  if (result == NULL) {
    gpr_log(GPR_ERROR, "Cannot find public key in X509 cert.");
  }

end:
  BIO_free(bio);
  if (x509 != NULL) X509_free(x509);
  return result;
}

static BIGNUM *bignum_from_base64(const char *b64) {
  BIGNUM *result = NULL;
  gpr_slice bin;

  if (b64 == NULL) return NULL;
  bin = grpc_base64_decode(b64, 1);
  if (GPR_SLICE_IS_EMPTY(bin)) {
    gpr_log(GPR_ERROR, "Invalid base64 for big num.");
    return NULL;
  }
  result = BN_bin2bn(GPR_SLICE_START_PTR(bin),
                     TSI_SIZE_AS_SIZE(GPR_SLICE_LENGTH(bin)), NULL);
  gpr_slice_unref(bin);
  return result;
}

static EVP_PKEY *pkey_from_jwk(const grpc_json *json, const char *kty) {
  const grpc_json *key_prop;
  RSA *rsa = NULL;
  EVP_PKEY *result = NULL;

  GPR_ASSERT(kty != NULL && json != NULL);
  if (strcmp(kty, "RSA") != 0) {
    gpr_log(GPR_ERROR, "Unsupported key type %s.", kty);
    goto end;
  }
  rsa = RSA_new();
  if (rsa == NULL) {
    gpr_log(GPR_ERROR, "Could not create rsa key.");
    goto end;
  }
  for (key_prop = json->child; key_prop != NULL; key_prop = key_prop->next) {
    if (strcmp(key_prop->key, "n") == 0) {
      rsa->n = bignum_from_base64(validate_string_field(key_prop, "n"));
      if (rsa->n == NULL) goto end;
    } else if (strcmp(key_prop->key, "e") == 0) {
      rsa->e = bignum_from_base64(validate_string_field(key_prop, "e"));
      if (rsa->e == NULL) goto end;
    }
  }
  if (rsa->e == NULL || rsa->n == NULL) {
    gpr_log(GPR_ERROR, "Missing RSA public key field.");
    goto end;
  }
  result = EVP_PKEY_new();
  EVP_PKEY_set1_RSA(result, rsa); /* uprefs rsa. */

end:
  if (rsa != NULL) RSA_free(rsa);
  return result;
}

static EVP_PKEY *find_verification_key(const grpc_json *json,
                                       const char *header_alg,
                                       const char *header_kid) {
  const grpc_json *jkey;
  const grpc_json *jwk_keys;
  /* Try to parse the json as a JWK set:
     https://tools.ietf.org/html/rfc7517#section-5. */
  jwk_keys = find_property_by_name(json, "keys");
  if (jwk_keys == NULL) {
    /* Use the google proprietary format which is:
       { <kid1>: <x5091>, <kid2>: <x5092>, ... } */
    const grpc_json *cur = find_property_by_name(json, header_kid);
    if (cur == NULL) return NULL;
    return extract_pkey_from_x509(cur->value);
  }

  if (jwk_keys->type != GRPC_JSON_ARRAY) {
    gpr_log(GPR_ERROR,
            "Unexpected value type of keys property in jwks key set.");
    return NULL;
  }
  /* Key format is specified in:
     https://tools.ietf.org/html/rfc7518#section-6. */
  for (jkey = jwk_keys->child; jkey != NULL; jkey = jkey->next) {
    grpc_json *key_prop;
    const char *alg = NULL;
    const char *kid = NULL;
    const char *kty = NULL;

    if (jkey->type != GRPC_JSON_OBJECT) continue;
    for (key_prop = jkey->child; key_prop != NULL; key_prop = key_prop->next) {
      if (strcmp(key_prop->key, "alg") == 0 &&
          key_prop->type == GRPC_JSON_STRING) {
        alg = key_prop->value;
      } else if (strcmp(key_prop->key, "kid") == 0 &&
                 key_prop->type == GRPC_JSON_STRING) {
        kid = key_prop->value;
      } else if (strcmp(key_prop->key, "kty") == 0 &&
                 key_prop->type == GRPC_JSON_STRING) {
        kty = key_prop->value;
      }
    }
    if (alg != NULL && kid != NULL && kty != NULL &&
        strcmp(kid, header_kid) == 0 && strcmp(alg, header_alg) == 0) {
      return pkey_from_jwk(jkey, kty);
    }
  }
  gpr_log(GPR_ERROR,
          "Could not find matching key in key set for kid=%s and alg=%s",
          header_kid, header_alg);
  return NULL;
}

static int verify_jwt_signature(EVP_PKEY *key, const char *alg,
                                gpr_slice signature, gpr_slice signed_data) {
  EVP_MD_CTX *md_ctx = EVP_MD_CTX_create();
  const EVP_MD *md = evp_md_from_alg(alg);
  int result = 0;

  GPR_ASSERT(md != NULL); /* Checked before. */
  if (md_ctx == NULL) {
    gpr_log(GPR_ERROR, "Could not create EVP_MD_CTX.");
    goto end;
  }
  if (EVP_DigestVerifyInit(md_ctx, NULL, md, NULL, key) != 1) {
    gpr_log(GPR_ERROR, "EVP_DigestVerifyInit failed.");
    goto end;
  }
  if (EVP_DigestVerifyUpdate(md_ctx, GPR_SLICE_START_PTR(signed_data),
                             GPR_SLICE_LENGTH(signed_data)) != 1) {
    gpr_log(GPR_ERROR, "EVP_DigestVerifyUpdate failed.");
    goto end;
  }
  if (EVP_DigestVerifyFinal(md_ctx, GPR_SLICE_START_PTR(signature),
                            GPR_SLICE_LENGTH(signature)) != 1) {
    gpr_log(GPR_ERROR, "JWT signature verification failed.");
    goto end;
  }
  result = 1;

end:
  if (md_ctx != NULL) EVP_MD_CTX_destroy(md_ctx);
  return result;
}

static void on_keys_retrieved(grpc_exec_ctx *exec_ctx, void *user_data,
                              grpc_error *error) {
  verifier_cb_ctx *ctx = (verifier_cb_ctx *)user_data;
  grpc_json *json = json_from_http(&ctx->responses[HTTP_RESPONSE_KEYS]);
  EVP_PKEY *verification_key = NULL;
  grpc_jwt_verifier_status status = GRPC_JWT_VERIFIER_GENERIC_ERROR;
  grpc_jwt_claims *claims = NULL;

  if (json == NULL) {
    status = GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR;
    goto end;
  }
  verification_key =
      find_verification_key(json, ctx->header->alg, ctx->header->kid);
  if (verification_key == NULL) {
    gpr_log(GPR_ERROR, "Could not find verification key with kid %s.",
            ctx->header->kid);
    status = GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR;
    goto end;
  }

  if (!verify_jwt_signature(verification_key, ctx->header->alg, ctx->signature,
                            ctx->signed_data)) {
    status = GRPC_JWT_VERIFIER_BAD_SIGNATURE;
    goto end;
  }

  status = grpc_jwt_claims_check(ctx->claims, ctx->audience);
  if (status == GRPC_JWT_VERIFIER_OK) {
    /* Pass ownership. */
    claims = ctx->claims;
    ctx->claims = NULL;
  }

end:
  if (json != NULL) grpc_json_destroy(json);
  if (verification_key != NULL) EVP_PKEY_free(verification_key);
  ctx->user_cb(ctx->user_data, status, claims);
  verifier_cb_ctx_destroy(ctx);
}

static void on_openid_config_retrieved(grpc_exec_ctx *exec_ctx, void *user_data,
                                       grpc_error *error) {
  const grpc_json *cur;
  verifier_cb_ctx *ctx = (verifier_cb_ctx *)user_data;
  const grpc_http_response *response = &ctx->responses[HTTP_RESPONSE_OPENID];
  grpc_json *json = json_from_http(response);
  grpc_httpcli_request req;
  const char *jwks_uri;

  /* TODO(jboeuf): Cache the jwks_uri in order to avoid this hop next time. */
  if (json == NULL) goto error;
  cur = find_property_by_name(json, "jwks_uri");
  if (cur == NULL) {
    gpr_log(GPR_ERROR, "Could not find jwks_uri in openid config.");
    goto error;
  }
  jwks_uri = validate_string_field(cur, "jwks_uri");
  if (jwks_uri == NULL) goto error;
  if (strstr(jwks_uri, "https://") != jwks_uri) {
    gpr_log(GPR_ERROR, "Invalid non https jwks_uri: %s.", jwks_uri);
    goto error;
  }
  jwks_uri += 8;
  req.handshaker = &grpc_httpcli_ssl;
  req.host = gpr_strdup(jwks_uri);
  req.http.path = strchr(jwks_uri, '/');
  if (req.http.path == NULL) {
    req.http.path = "";
  } else {
    *(req.host + (req.http.path - jwks_uri)) = '\0';
  }

  grpc_httpcli_get(
      exec_ctx, &ctx->verifier->http_ctx, &ctx->pollent, &req,
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), grpc_jwt_verifier_max_delay),
      grpc_closure_create(on_keys_retrieved, ctx),
      &ctx->responses[HTTP_RESPONSE_KEYS]);
  grpc_json_destroy(json);
  gpr_free(req.host);
  return;

error:
  if (json != NULL) grpc_json_destroy(json);
  ctx->user_cb(ctx->user_data, GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR, NULL);
  verifier_cb_ctx_destroy(ctx);
}

static email_key_mapping *verifier_get_mapping(grpc_jwt_verifier *v,
                                               const char *email_domain) {
  size_t i;
  if (v->mappings == NULL) return NULL;
  for (i = 0; i < v->num_mappings; i++) {
    if (strcmp(email_domain, v->mappings[i].email_domain) == 0) {
      return &v->mappings[i];
    }
  }
  return NULL;
}

static void verifier_put_mapping(grpc_jwt_verifier *v, const char *email_domain,
                                 const char *key_url_prefix) {
  email_key_mapping *mapping = verifier_get_mapping(v, email_domain);
  GPR_ASSERT(v->num_mappings < v->allocated_mappings);
  if (mapping != NULL) {
    gpr_free(mapping->key_url_prefix);
    mapping->key_url_prefix = gpr_strdup(key_url_prefix);
    return;
  }
  v->mappings[v->num_mappings].email_domain = gpr_strdup(email_domain);
  v->mappings[v->num_mappings].key_url_prefix = gpr_strdup(key_url_prefix);
  v->num_mappings++;
  GPR_ASSERT(v->num_mappings <= v->allocated_mappings);
}

/* Takes ownership of ctx. */
static void retrieve_key_and_verify(grpc_exec_ctx *exec_ctx,
                                    verifier_cb_ctx *ctx) {
  const char *at_sign;
  grpc_closure *http_cb;
  char *path_prefix = NULL;
  const char *iss;
  grpc_httpcli_request req;
  memset(&req, 0, sizeof(grpc_httpcli_request));
  req.handshaker = &grpc_httpcli_ssl;
  http_response_index rsp_idx;

  GPR_ASSERT(ctx != NULL && ctx->header != NULL && ctx->claims != NULL);
  iss = ctx->claims->iss;
  if (ctx->header->kid == NULL) {
    gpr_log(GPR_ERROR, "Missing kid in jose header.");
    goto error;
  }
  if (iss == NULL) {
    gpr_log(GPR_ERROR, "Missing iss in claims.");
    goto error;
  }

  /* This code relies on:
     https://openid.net/specs/openid-connect-discovery-1_0.html
     Nobody seems to implement the account/email/webfinger part 2. of the spec
     so we will rely instead on email/url mappings if we detect such an issuer.
     Part 4, on the other hand is implemented by both google and salesforce. */

  /* Very non-sophisticated way to detect an email address. Should be good
     enough for now... */
  at_sign = strchr(iss, '@');
  if (at_sign != NULL) {
    email_key_mapping *mapping;
    const char *email_domain = at_sign + 1;
    GPR_ASSERT(ctx->verifier != NULL);
    mapping = verifier_get_mapping(ctx->verifier, email_domain);
    if (mapping == NULL) {
      gpr_log(GPR_ERROR, "Missing mapping for issuer email.");
      goto error;
    }
    req.host = gpr_strdup(mapping->key_url_prefix);
    path_prefix = strchr(req.host, '/');
    if (path_prefix == NULL) {
      gpr_asprintf(&req.http.path, "/%s", iss);
    } else {
      *(path_prefix++) = '\0';
      gpr_asprintf(&req.http.path, "/%s/%s", path_prefix, iss);
    }
    http_cb = grpc_closure_create(on_keys_retrieved, ctx);
    rsp_idx = HTTP_RESPONSE_KEYS;
  } else {
    req.host = gpr_strdup(strstr(iss, "https://") == iss ? iss + 8 : iss);
    path_prefix = strchr(req.host, '/');
    if (path_prefix == NULL) {
      req.http.path = gpr_strdup(GRPC_OPENID_CONFIG_URL_SUFFIX);
    } else {
      *(path_prefix++) = 0;
      gpr_asprintf(&req.http.path, "/%s%s", path_prefix,
                   GRPC_OPENID_CONFIG_URL_SUFFIX);
    }
    http_cb = grpc_closure_create(on_openid_config_retrieved, ctx);
    rsp_idx = HTTP_RESPONSE_OPENID;
  }

  grpc_httpcli_get(
      exec_ctx, &ctx->verifier->http_ctx, &ctx->pollent, &req,
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), grpc_jwt_verifier_max_delay),
      http_cb, &ctx->responses[rsp_idx]);
  gpr_free(req.host);
  gpr_free(req.http.path);
  return;

error:
  ctx->user_cb(ctx->user_data, GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR, NULL);
  verifier_cb_ctx_destroy(ctx);
}

void grpc_jwt_verifier_verify(grpc_exec_ctx *exec_ctx,
                              grpc_jwt_verifier *verifier,
                              grpc_pollset *pollset, const char *jwt,
                              const char *audience,
                              grpc_jwt_verification_done_cb cb,
                              void *user_data) {
  const char *dot = NULL;
  grpc_json *json;
  jose_header *header = NULL;
  grpc_jwt_claims *claims = NULL;
  gpr_slice header_buffer;
  gpr_slice claims_buffer;
  gpr_slice signature;
  size_t signed_jwt_len;
  const char *cur = jwt;

  GPR_ASSERT(verifier != NULL && jwt != NULL && audience != NULL && cb != NULL);
  dot = strchr(cur, '.');
  if (dot == NULL) goto error;
  json = parse_json_part_from_jwt(cur, (size_t)(dot - cur), &header_buffer);
  if (json == NULL) goto error;
  header = jose_header_from_json(json, header_buffer);
  if (header == NULL) goto error;

  cur = dot + 1;
  dot = strchr(cur, '.');
  if (dot == NULL) goto error;
  json = parse_json_part_from_jwt(cur, (size_t)(dot - cur), &claims_buffer);
  if (json == NULL) goto error;
  claims = grpc_jwt_claims_from_json(json, claims_buffer);
  if (claims == NULL) goto error;

  signed_jwt_len = (size_t)(dot - jwt);
  cur = dot + 1;
  signature = grpc_base64_decode(cur, 1);
  if (GPR_SLICE_IS_EMPTY(signature)) goto error;
  retrieve_key_and_verify(
      exec_ctx,
      verifier_cb_ctx_create(verifier, pollset, header, claims, audience,
                             signature, jwt, signed_jwt_len, user_data, cb));
  return;

error:
  if (header != NULL) jose_header_destroy(header);
  if (claims != NULL) grpc_jwt_claims_destroy(claims);
  cb(user_data, GRPC_JWT_VERIFIER_BAD_FORMAT, NULL);
}

grpc_jwt_verifier *grpc_jwt_verifier_create(
    const grpc_jwt_verifier_email_domain_key_url_mapping *mappings,
    size_t num_mappings) {
  grpc_jwt_verifier *v = gpr_malloc(sizeof(grpc_jwt_verifier));
  memset(v, 0, sizeof(grpc_jwt_verifier));
  grpc_httpcli_context_init(&v->http_ctx);

  /* We know at least of one mapping. */
  v->allocated_mappings = 1 + num_mappings;
  v->mappings = gpr_malloc(v->allocated_mappings * sizeof(email_key_mapping));
  verifier_put_mapping(v, GRPC_GOOGLE_SERVICE_ACCOUNTS_EMAIL_DOMAIN,
                       GRPC_GOOGLE_SERVICE_ACCOUNTS_KEY_URL_PREFIX);
  /* User-Provided mappings. */
  if (mappings != NULL) {
    size_t i;
    for (i = 0; i < num_mappings; i++) {
      verifier_put_mapping(v, mappings[i].email_domain,
                           mappings[i].key_url_prefix);
    }
  }
  return v;
}

void grpc_jwt_verifier_destroy(grpc_jwt_verifier *v) {
  size_t i;
  if (v == NULL) return;
  grpc_httpcli_context_destroy(&v->http_ctx);
  if (v->mappings != NULL) {
    for (i = 0; i < v->num_mappings; i++) {
      gpr_free(v->mappings[i].email_domain);
      gpr_free(v->mappings[i].key_url_prefix);
    }
    gpr_free(v->mappings);
  }
  gpr_free(v);
}
