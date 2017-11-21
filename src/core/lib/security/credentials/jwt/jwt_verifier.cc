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

#include "src/core/lib/security/credentials/jwt/jwt_verifier.h"

#include <limits.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

extern "C" {
#include <openssl/pem.h>
}

#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/slice/b64.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/support/string.h"
#include "src/core/tsi/ssl_types.h"

/* --- Utils. --- */

const char* grpc_jwt_verifier_status_to_string(
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

static const EVP_MD* evp_md_from_alg(const char* alg) {
  if (strcmp(alg, "RS256") == 0) {
    return EVP_sha256();
  } else if (strcmp(alg, "RS384") == 0) {
    return EVP_sha384();
  } else if (strcmp(alg, "RS512") == 0) {
    return EVP_sha512();
  } else {
    return nullptr;
  }
}

static grpc_json* parse_json_part_from_jwt(grpc_exec_ctx* exec_ctx,
                                           const char* str, size_t len,
                                           grpc_slice* buffer) {
  grpc_json* json;

  *buffer = grpc_base64_decode_with_len(exec_ctx, str, len, 1);
  if (GRPC_SLICE_IS_EMPTY(*buffer)) {
    gpr_log(GPR_ERROR, "Invalid base64.");
    return nullptr;
  }
  json = grpc_json_parse_string_with_len((char*)GRPC_SLICE_START_PTR(*buffer),
                                         GRPC_SLICE_LENGTH(*buffer));
  if (json == nullptr) {
    grpc_slice_unref_internal(exec_ctx, *buffer);
    gpr_log(GPR_ERROR, "JSON parsing error.");
  }
  return json;
}

static const char* validate_string_field(const grpc_json* json,
                                         const char* key) {
  if (json->type != GRPC_JSON_STRING) {
    gpr_log(GPR_ERROR, "Invalid %s field [%s]", key, json->value);
    return nullptr;
  }
  return json->value;
}

static gpr_timespec validate_time_field(const grpc_json* json,
                                        const char* key) {
  gpr_timespec result = gpr_time_0(GPR_CLOCK_REALTIME);
  if (json->type != GRPC_JSON_NUMBER) {
    gpr_log(GPR_ERROR, "Invalid %s field [%s]", key, json->value);
    return result;
  }
  result.tv_sec = strtol(json->value, nullptr, 10);
  return result;
}

/* --- JOSE header. see http://tools.ietf.org/html/rfc7515#section-4 --- */

typedef struct {
  const char* alg;
  const char* kid;
  const char* typ;
  /* TODO(jboeuf): Add others as needed (jku, jwk, x5u, x5c and so on...). */
  grpc_slice buffer;
} jose_header;

static void jose_header_destroy(grpc_exec_ctx* exec_ctx, jose_header* h) {
  grpc_slice_unref_internal(exec_ctx, h->buffer);
  gpr_free(h);
}

/* Takes ownership of json and buffer. */
static jose_header* jose_header_from_json(grpc_exec_ctx* exec_ctx,
                                          grpc_json* json, grpc_slice buffer) {
  grpc_json* cur;
  jose_header* h = (jose_header*)gpr_zalloc(sizeof(jose_header));
  h->buffer = buffer;
  for (cur = json->child; cur != nullptr; cur = cur->next) {
    if (strcmp(cur->key, "alg") == 0) {
      /* We only support RSA-1.5 signatures for now.
         Beware of this if we add HMAC support:
         https://auth0.com/blog/2015/03/31/critical-vulnerabilities-in-json-web-token-libraries/
       */
      if (cur->type != GRPC_JSON_STRING || strncmp(cur->value, "RS", 2) ||
          evp_md_from_alg(cur->value) == nullptr) {
        gpr_log(GPR_ERROR, "Invalid alg field [%s]", cur->value);
        goto error;
      }
      h->alg = cur->value;
    } else if (strcmp(cur->key, "typ") == 0) {
      h->typ = validate_string_field(cur, "typ");
      if (h->typ == nullptr) goto error;
    } else if (strcmp(cur->key, "kid") == 0) {
      h->kid = validate_string_field(cur, "kid");
      if (h->kid == nullptr) goto error;
    }
  }
  if (h->alg == nullptr) {
    gpr_log(GPR_ERROR, "Missing alg field.");
    goto error;
  }
  grpc_json_destroy(json);
  h->buffer = buffer;
  return h;

error:
  grpc_json_destroy(json);
  jose_header_destroy(exec_ctx, h);
  return nullptr;
}

/* --- JWT claims. see http://tools.ietf.org/html/rfc7519#section-4.1 */

struct grpc_jwt_claims {
  /* Well known properties already parsed. */
  const char* sub;
  const char* iss;
  const char* aud;
  const char* jti;
  gpr_timespec iat;
  gpr_timespec exp;
  gpr_timespec nbf;

  grpc_json* json;
  grpc_slice buffer;
};

void grpc_jwt_claims_destroy(grpc_exec_ctx* exec_ctx, grpc_jwt_claims* claims) {
  grpc_json_destroy(claims->json);
  grpc_slice_unref_internal(exec_ctx, claims->buffer);
  gpr_free(claims);
}

const grpc_json* grpc_jwt_claims_json(const grpc_jwt_claims* claims) {
  if (claims == nullptr) return nullptr;
  return claims->json;
}

const char* grpc_jwt_claims_subject(const grpc_jwt_claims* claims) {
  if (claims == nullptr) return nullptr;
  return claims->sub;
}

const char* grpc_jwt_claims_issuer(const grpc_jwt_claims* claims) {
  if (claims == nullptr) return nullptr;
  return claims->iss;
}

const char* grpc_jwt_claims_id(const grpc_jwt_claims* claims) {
  if (claims == nullptr) return nullptr;
  return claims->jti;
}

const char* grpc_jwt_claims_audience(const grpc_jwt_claims* claims) {
  if (claims == nullptr) return nullptr;
  return claims->aud;
}

gpr_timespec grpc_jwt_claims_issued_at(const grpc_jwt_claims* claims) {
  if (claims == nullptr) return gpr_inf_past(GPR_CLOCK_REALTIME);
  return claims->iat;
}

gpr_timespec grpc_jwt_claims_expires_at(const grpc_jwt_claims* claims) {
  if (claims == nullptr) return gpr_inf_future(GPR_CLOCK_REALTIME);
  return claims->exp;
}

gpr_timespec grpc_jwt_claims_not_before(const grpc_jwt_claims* claims) {
  if (claims == nullptr) return gpr_inf_past(GPR_CLOCK_REALTIME);
  return claims->nbf;
}

/* Takes ownership of json and buffer even in case of failure. */
grpc_jwt_claims* grpc_jwt_claims_from_json(grpc_exec_ctx* exec_ctx,
                                           grpc_json* json, grpc_slice buffer) {
  grpc_json* cur;
  grpc_jwt_claims* claims =
      (grpc_jwt_claims*)gpr_malloc(sizeof(grpc_jwt_claims));
  memset(claims, 0, sizeof(grpc_jwt_claims));
  claims->json = json;
  claims->buffer = buffer;
  claims->iat = gpr_inf_past(GPR_CLOCK_REALTIME);
  claims->nbf = gpr_inf_past(GPR_CLOCK_REALTIME);
  claims->exp = gpr_inf_future(GPR_CLOCK_REALTIME);

  /* Per the spec, all fields are optional. */
  for (cur = json->child; cur != nullptr; cur = cur->next) {
    if (strcmp(cur->key, "sub") == 0) {
      claims->sub = validate_string_field(cur, "sub");
      if (claims->sub == nullptr) goto error;
    } else if (strcmp(cur->key, "iss") == 0) {
      claims->iss = validate_string_field(cur, "iss");
      if (claims->iss == nullptr) goto error;
    } else if (strcmp(cur->key, "aud") == 0) {
      claims->aud = validate_string_field(cur, "aud");
      if (claims->aud == nullptr) goto error;
    } else if (strcmp(cur->key, "jti") == 0) {
      claims->jti = validate_string_field(cur, "jti");
      if (claims->jti == nullptr) goto error;
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
  grpc_jwt_claims_destroy(exec_ctx, claims);
  return nullptr;
}

grpc_jwt_verifier_status grpc_jwt_claims_check(const grpc_jwt_claims* claims,
                                               const char* audience) {
  gpr_timespec skewed_now;
  int audience_ok;

  GPR_ASSERT(claims != nullptr);

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

  /* This should be probably up to the upper layer to decide but let's harcode
     the 99% use case here for email issuers, where the JWT must be self
     issued. */
  if (grpc_jwt_issuer_email_domain(claims->iss) != nullptr &&
      claims->sub != nullptr && strcmp(claims->iss, claims->sub) != 0) {
    gpr_log(GPR_ERROR,
            "Email issuer (%s) cannot assert another subject (%s) than itself.",
            claims->iss, claims->sub);
    return GRPC_JWT_VERIFIER_BAD_SUBJECT;
  }

  if (audience == nullptr) {
    audience_ok = claims->aud == nullptr;
  } else {
    audience_ok = claims->aud != nullptr && strcmp(audience, claims->aud) == 0;
  }
  if (!audience_ok) {
    gpr_log(GPR_ERROR, "Audience mismatch: expected %s and found %s.",
            audience == nullptr ? "NULL" : audience,
            claims->aud == nullptr ? "NULL" : claims->aud);
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
  grpc_jwt_verifier* verifier;
  grpc_polling_entity pollent;
  jose_header* header;
  grpc_jwt_claims* claims;
  char* audience;
  grpc_slice signature;
  grpc_slice signed_data;
  void* user_data;
  grpc_jwt_verification_done_cb user_cb;
  grpc_http_response responses[HTTP_RESPONSE_COUNT];
} verifier_cb_ctx;

/* Takes ownership of the header, claims and signature. */
static verifier_cb_ctx* verifier_cb_ctx_create(
    grpc_jwt_verifier* verifier, grpc_pollset* pollset, jose_header* header,
    grpc_jwt_claims* claims, const char* audience, grpc_slice signature,
    const char* signed_jwt, size_t signed_jwt_len, void* user_data,
    grpc_jwt_verification_done_cb cb) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  verifier_cb_ctx* ctx = (verifier_cb_ctx*)gpr_zalloc(sizeof(verifier_cb_ctx));
  ctx->verifier = verifier;
  ctx->pollent = grpc_polling_entity_create_from_pollset(pollset);
  ctx->header = header;
  ctx->audience = gpr_strdup(audience);
  ctx->claims = claims;
  ctx->signature = signature;
  ctx->signed_data = grpc_slice_from_copied_buffer(signed_jwt, signed_jwt_len);
  ctx->user_data = user_data;
  ctx->user_cb = cb;
  grpc_exec_ctx_finish(&exec_ctx);
  return ctx;
}

void verifier_cb_ctx_destroy(grpc_exec_ctx* exec_ctx, verifier_cb_ctx* ctx) {
  if (ctx->audience != nullptr) gpr_free(ctx->audience);
  if (ctx->claims != nullptr) grpc_jwt_claims_destroy(exec_ctx, ctx->claims);
  grpc_slice_unref_internal(exec_ctx, ctx->signature);
  grpc_slice_unref_internal(exec_ctx, ctx->signed_data);
  jose_header_destroy(exec_ctx, ctx->header);
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
grpc_millis grpc_jwt_verifier_max_delay = 60 * GPR_MS_PER_SEC;

typedef struct {
  char* email_domain;
  char* key_url_prefix;
} email_key_mapping;

struct grpc_jwt_verifier {
  email_key_mapping* mappings;
  size_t num_mappings; /* Should be very few, linear search ok. */
  size_t allocated_mappings;
  grpc_httpcli_context http_ctx;
};

static grpc_json* json_from_http(const grpc_httpcli_response* response) {
  grpc_json* json = nullptr;

  if (response == nullptr) {
    gpr_log(GPR_ERROR, "HTTP response is NULL.");
    return nullptr;
  }
  if (response->status != 200) {
    gpr_log(GPR_ERROR, "Call to http server failed with error %d.",
            response->status);
    return nullptr;
  }

  json = grpc_json_parse_string_with_len(response->body, response->body_length);
  if (json == nullptr) {
    gpr_log(GPR_ERROR, "Invalid JSON found in response.");
  }
  return json;
}

static const grpc_json* find_property_by_name(const grpc_json* json,
                                              const char* name) {
  const grpc_json* cur;
  for (cur = json->child; cur != nullptr; cur = cur->next) {
    if (strcmp(cur->key, name) == 0) return cur;
  }
  return nullptr;
}

static EVP_PKEY* extract_pkey_from_x509(const char* x509_str) {
  X509* x509 = nullptr;
  EVP_PKEY* result = nullptr;
  BIO* bio = BIO_new(BIO_s_mem());
  size_t len = strlen(x509_str);
  GPR_ASSERT(len < INT_MAX);
  BIO_write(bio, x509_str, (int)len);
  x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  if (x509 == nullptr) {
    gpr_log(GPR_ERROR, "Unable to parse x509 cert.");
    goto end;
  }
  result = X509_get_pubkey(x509);
  if (result == nullptr) {
    gpr_log(GPR_ERROR, "Cannot find public key in X509 cert.");
  }

end:
  BIO_free(bio);
  X509_free(x509);
  return result;
}

static BIGNUM* bignum_from_base64(grpc_exec_ctx* exec_ctx, const char* b64) {
  BIGNUM* result = nullptr;
  grpc_slice bin;

  if (b64 == nullptr) return nullptr;
  bin = grpc_base64_decode(exec_ctx, b64, 1);
  if (GRPC_SLICE_IS_EMPTY(bin)) {
    gpr_log(GPR_ERROR, "Invalid base64 for big num.");
    return nullptr;
  }
  result = BN_bin2bn(GRPC_SLICE_START_PTR(bin),
                     TSI_SIZE_AS_SIZE(GRPC_SLICE_LENGTH(bin)), nullptr);
  grpc_slice_unref_internal(exec_ctx, bin);
  return result;
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L

// Provide compatibility across OpenSSL 1.02 and 1.1.
static int RSA_set0_key(RSA* r, BIGNUM* n, BIGNUM* e, BIGNUM* d) {
  /* If the fields n and e in r are NULL, the corresponding input
   * parameters MUST be non-NULL for n and e.  d may be
   * left NULL (in case only the public key is used).
   */
  if ((r->n == nullptr && n == nullptr) || (r->e == nullptr && e == nullptr)) {
    return 0;
  }

  if (n != nullptr) {
    BN_free(r->n);
    r->n = n;
  }
  if (e != nullptr) {
    BN_free(r->e);
    r->e = e;
  }
  if (d != nullptr) {
    BN_free(r->d);
    r->d = d;
  }

  return 1;
}
#endif  // OPENSSL_VERSION_NUMBER < 0x10100000L

static EVP_PKEY* pkey_from_jwk(grpc_exec_ctx* exec_ctx, const grpc_json* json,
                               const char* kty) {
  const grpc_json* key_prop;
  RSA* rsa = nullptr;
  EVP_PKEY* result = nullptr;
  BIGNUM* tmp_n = nullptr;
  BIGNUM* tmp_e = nullptr;

  GPR_ASSERT(kty != nullptr && json != nullptr);
  if (strcmp(kty, "RSA") != 0) {
    gpr_log(GPR_ERROR, "Unsupported key type %s.", kty);
    goto end;
  }
  rsa = RSA_new();
  if (rsa == nullptr) {
    gpr_log(GPR_ERROR, "Could not create rsa key.");
    goto end;
  }
  for (key_prop = json->child; key_prop != nullptr; key_prop = key_prop->next) {
    if (strcmp(key_prop->key, "n") == 0) {
      tmp_n =
          bignum_from_base64(exec_ctx, validate_string_field(key_prop, "n"));
      if (tmp_n == nullptr) goto end;
    } else if (strcmp(key_prop->key, "e") == 0) {
      tmp_e =
          bignum_from_base64(exec_ctx, validate_string_field(key_prop, "e"));
      if (tmp_e == nullptr) goto end;
    }
  }
  if (tmp_e == nullptr || tmp_n == nullptr) {
    gpr_log(GPR_ERROR, "Missing RSA public key field.");
    goto end;
  }
  if (!RSA_set0_key(rsa, tmp_n, tmp_e, nullptr)) {
    gpr_log(GPR_ERROR, "Cannot set RSA key from inputs.");
    goto end;
  }
  /* RSA_set0_key takes ownership on success. */
  tmp_n = nullptr;
  tmp_e = nullptr;
  result = EVP_PKEY_new();
  EVP_PKEY_set1_RSA(result, rsa); /* uprefs rsa. */

end:
  RSA_free(rsa);
  BN_free(tmp_n);
  BN_free(tmp_e);
  return result;
}

static EVP_PKEY* find_verification_key(grpc_exec_ctx* exec_ctx,
                                       const grpc_json* json,
                                       const char* header_alg,
                                       const char* header_kid) {
  const grpc_json* jkey;
  const grpc_json* jwk_keys;
  /* Try to parse the json as a JWK set:
     https://tools.ietf.org/html/rfc7517#section-5. */
  jwk_keys = find_property_by_name(json, "keys");
  if (jwk_keys == nullptr) {
    /* Use the google proprietary format which is:
       { <kid1>: <x5091>, <kid2>: <x5092>, ... } */
    const grpc_json* cur = find_property_by_name(json, header_kid);
    if (cur == nullptr) return nullptr;
    return extract_pkey_from_x509(cur->value);
  }

  if (jwk_keys->type != GRPC_JSON_ARRAY) {
    gpr_log(GPR_ERROR,
            "Unexpected value type of keys property in jwks key set.");
    return nullptr;
  }
  /* Key format is specified in:
     https://tools.ietf.org/html/rfc7518#section-6. */
  for (jkey = jwk_keys->child; jkey != nullptr; jkey = jkey->next) {
    grpc_json* key_prop;
    const char* alg = nullptr;
    const char* kid = nullptr;
    const char* kty = nullptr;

    if (jkey->type != GRPC_JSON_OBJECT) continue;
    for (key_prop = jkey->child; key_prop != nullptr;
         key_prop = key_prop->next) {
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
    if (alg != nullptr && kid != nullptr && kty != nullptr &&
        strcmp(kid, header_kid) == 0 && strcmp(alg, header_alg) == 0) {
      return pkey_from_jwk(exec_ctx, jkey, kty);
    }
  }
  gpr_log(GPR_ERROR,
          "Could not find matching key in key set for kid=%s and alg=%s",
          header_kid, header_alg);
  return nullptr;
}

static int verify_jwt_signature(EVP_PKEY* key, const char* alg,
                                grpc_slice signature, grpc_slice signed_data) {
  EVP_MD_CTX* md_ctx = EVP_MD_CTX_create();
  const EVP_MD* md = evp_md_from_alg(alg);
  int result = 0;

  GPR_ASSERT(md != nullptr); /* Checked before. */
  if (md_ctx == nullptr) {
    gpr_log(GPR_ERROR, "Could not create EVP_MD_CTX.");
    goto end;
  }
  if (EVP_DigestVerifyInit(md_ctx, nullptr, md, nullptr, key) != 1) {
    gpr_log(GPR_ERROR, "EVP_DigestVerifyInit failed.");
    goto end;
  }
  if (EVP_DigestVerifyUpdate(md_ctx, GRPC_SLICE_START_PTR(signed_data),
                             GRPC_SLICE_LENGTH(signed_data)) != 1) {
    gpr_log(GPR_ERROR, "EVP_DigestVerifyUpdate failed.");
    goto end;
  }
  if (EVP_DigestVerifyFinal(md_ctx, GRPC_SLICE_START_PTR(signature),
                            GRPC_SLICE_LENGTH(signature)) != 1) {
    gpr_log(GPR_ERROR, "JWT signature verification failed.");
    goto end;
  }
  result = 1;

end:
  EVP_MD_CTX_destroy(md_ctx);
  return result;
}

static void on_keys_retrieved(grpc_exec_ctx* exec_ctx, void* user_data,
                              grpc_error* error) {
  verifier_cb_ctx* ctx = (verifier_cb_ctx*)user_data;
  grpc_json* json = json_from_http(&ctx->responses[HTTP_RESPONSE_KEYS]);
  EVP_PKEY* verification_key = nullptr;
  grpc_jwt_verifier_status status = GRPC_JWT_VERIFIER_GENERIC_ERROR;
  grpc_jwt_claims* claims = nullptr;

  if (json == nullptr) {
    status = GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR;
    goto end;
  }
  verification_key =
      find_verification_key(exec_ctx, json, ctx->header->alg, ctx->header->kid);
  if (verification_key == nullptr) {
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
    ctx->claims = nullptr;
  }

end:
  if (json != nullptr) grpc_json_destroy(json);
  EVP_PKEY_free(verification_key);
  ctx->user_cb(exec_ctx, ctx->user_data, status, claims);
  verifier_cb_ctx_destroy(exec_ctx, ctx);
}

static void on_openid_config_retrieved(grpc_exec_ctx* exec_ctx, void* user_data,
                                       grpc_error* error) {
  const grpc_json* cur;
  verifier_cb_ctx* ctx = (verifier_cb_ctx*)user_data;
  const grpc_http_response* response = &ctx->responses[HTTP_RESPONSE_OPENID];
  grpc_json* json = json_from_http(response);
  grpc_httpcli_request req;
  const char* jwks_uri;
  grpc_resource_quota* resource_quota = nullptr;

  /* TODO(jboeuf): Cache the jwks_uri in order to avoid this hop next time. */
  if (json == nullptr) goto error;
  cur = find_property_by_name(json, "jwks_uri");
  if (cur == nullptr) {
    gpr_log(GPR_ERROR, "Could not find jwks_uri in openid config.");
    goto error;
  }
  jwks_uri = validate_string_field(cur, "jwks_uri");
  if (jwks_uri == nullptr) goto error;
  if (strstr(jwks_uri, "https://") != jwks_uri) {
    gpr_log(GPR_ERROR, "Invalid non https jwks_uri: %s.", jwks_uri);
    goto error;
  }
  jwks_uri += 8;
  req.handshaker = &grpc_httpcli_ssl;
  req.host = gpr_strdup(jwks_uri);
  req.http.path = (char*)strchr(jwks_uri, '/');
  if (req.http.path == nullptr) {
    req.http.path = (char*)"";
  } else {
    *(req.host + (req.http.path - jwks_uri)) = '\0';
  }

  /* TODO(ctiller): Carry the resource_quota in ctx and share it with the host
     channel. This would allow us to cancel an authentication query when under
     extreme memory pressure. */
  resource_quota = grpc_resource_quota_create("jwt_verifier");
  grpc_httpcli_get(
      exec_ctx, &ctx->verifier->http_ctx, &ctx->pollent, resource_quota, &req,
      grpc_exec_ctx_now(exec_ctx) + grpc_jwt_verifier_max_delay,
      GRPC_CLOSURE_CREATE(on_keys_retrieved, ctx, grpc_schedule_on_exec_ctx),
      &ctx->responses[HTTP_RESPONSE_KEYS]);
  grpc_resource_quota_unref_internal(exec_ctx, resource_quota);
  grpc_json_destroy(json);
  gpr_free(req.host);
  return;

error:
  if (json != nullptr) grpc_json_destroy(json);
  ctx->user_cb(exec_ctx, ctx->user_data, GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR,
               nullptr);
  verifier_cb_ctx_destroy(exec_ctx, ctx);
}

static email_key_mapping* verifier_get_mapping(grpc_jwt_verifier* v,
                                               const char* email_domain) {
  size_t i;
  if (v->mappings == nullptr) return nullptr;
  for (i = 0; i < v->num_mappings; i++) {
    if (strcmp(email_domain, v->mappings[i].email_domain) == 0) {
      return &v->mappings[i];
    }
  }
  return nullptr;
}

static void verifier_put_mapping(grpc_jwt_verifier* v, const char* email_domain,
                                 const char* key_url_prefix) {
  email_key_mapping* mapping = verifier_get_mapping(v, email_domain);
  GPR_ASSERT(v->num_mappings < v->allocated_mappings);
  if (mapping != nullptr) {
    gpr_free(mapping->key_url_prefix);
    mapping->key_url_prefix = gpr_strdup(key_url_prefix);
    return;
  }
  v->mappings[v->num_mappings].email_domain = gpr_strdup(email_domain);
  v->mappings[v->num_mappings].key_url_prefix = gpr_strdup(key_url_prefix);
  v->num_mappings++;
  GPR_ASSERT(v->num_mappings <= v->allocated_mappings);
}

/* Very non-sophisticated way to detect an email address. Should be good
   enough for now... */
const char* grpc_jwt_issuer_email_domain(const char* issuer) {
  const char* at_sign = strchr(issuer, '@');
  if (at_sign == nullptr) return nullptr;
  const char* email_domain = at_sign + 1;
  if (*email_domain == '\0') return nullptr;
  const char* dot = strrchr(email_domain, '.');
  if (dot == nullptr || dot == email_domain) return email_domain;
  GPR_ASSERT(dot > email_domain);
  /* There may be a subdomain, we just want the domain. */
  dot = (const char*)gpr_memrchr((void*)email_domain, '.',
                                 (size_t)(dot - email_domain));
  if (dot == nullptr) return email_domain;
  return dot + 1;
}

/* Takes ownership of ctx. */
static void retrieve_key_and_verify(grpc_exec_ctx* exec_ctx,
                                    verifier_cb_ctx* ctx) {
  const char* email_domain;
  grpc_closure* http_cb;
  char* path_prefix = nullptr;
  const char* iss;
  grpc_httpcli_request req;
  grpc_resource_quota* resource_quota = nullptr;
  memset(&req, 0, sizeof(grpc_httpcli_request));
  req.handshaker = &grpc_httpcli_ssl;
  http_response_index rsp_idx;

  GPR_ASSERT(ctx != nullptr && ctx->header != nullptr &&
             ctx->claims != nullptr);
  iss = ctx->claims->iss;
  if (ctx->header->kid == nullptr) {
    gpr_log(GPR_ERROR, "Missing kid in jose header.");
    goto error;
  }
  if (iss == nullptr) {
    gpr_log(GPR_ERROR, "Missing iss in claims.");
    goto error;
  }

  /* This code relies on:
     https://openid.net/specs/openid-connect-discovery-1_0.html
     Nobody seems to implement the account/email/webfinger part 2. of the spec
     so we will rely instead on email/url mappings if we detect such an issuer.
     Part 4, on the other hand is implemented by both google and salesforce. */
  email_domain = grpc_jwt_issuer_email_domain(iss);
  if (email_domain != nullptr) {
    email_key_mapping* mapping;
    GPR_ASSERT(ctx->verifier != nullptr);
    mapping = verifier_get_mapping(ctx->verifier, email_domain);
    if (mapping == nullptr) {
      gpr_log(GPR_ERROR, "Missing mapping for issuer email.");
      goto error;
    }
    req.host = gpr_strdup(mapping->key_url_prefix);
    path_prefix = strchr(req.host, '/');
    if (path_prefix == nullptr) {
      gpr_asprintf(&req.http.path, "/%s", iss);
    } else {
      *(path_prefix++) = '\0';
      gpr_asprintf(&req.http.path, "/%s/%s", path_prefix, iss);
    }
    http_cb =
        GRPC_CLOSURE_CREATE(on_keys_retrieved, ctx, grpc_schedule_on_exec_ctx);
    rsp_idx = HTTP_RESPONSE_KEYS;
  } else {
    req.host = gpr_strdup(strstr(iss, "https://") == iss ? iss + 8 : iss);
    path_prefix = strchr(req.host, '/');
    if (path_prefix == nullptr) {
      req.http.path = gpr_strdup(GRPC_OPENID_CONFIG_URL_SUFFIX);
    } else {
      *(path_prefix++) = 0;
      gpr_asprintf(&req.http.path, "/%s%s", path_prefix,
                   GRPC_OPENID_CONFIG_URL_SUFFIX);
    }
    http_cb = GRPC_CLOSURE_CREATE(on_openid_config_retrieved, ctx,
                                  grpc_schedule_on_exec_ctx);
    rsp_idx = HTTP_RESPONSE_OPENID;
  }

  /* TODO(ctiller): Carry the resource_quota in ctx and share it with the host
     channel. This would allow us to cancel an authentication query when under
     extreme memory pressure. */
  resource_quota = grpc_resource_quota_create("jwt_verifier");
  grpc_httpcli_get(exec_ctx, &ctx->verifier->http_ctx, &ctx->pollent,
                   resource_quota, &req,
                   grpc_exec_ctx_now(exec_ctx) + grpc_jwt_verifier_max_delay,
                   http_cb, &ctx->responses[rsp_idx]);
  grpc_resource_quota_unref_internal(exec_ctx, resource_quota);
  gpr_free(req.host);
  gpr_free(req.http.path);
  return;

error:
  ctx->user_cb(exec_ctx, ctx->user_data, GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR,
               nullptr);
  verifier_cb_ctx_destroy(exec_ctx, ctx);
}

void grpc_jwt_verifier_verify(grpc_exec_ctx* exec_ctx,
                              grpc_jwt_verifier* verifier,
                              grpc_pollset* pollset, const char* jwt,
                              const char* audience,
                              grpc_jwt_verification_done_cb cb,
                              void* user_data) {
  const char* dot = nullptr;
  grpc_json* json;
  jose_header* header = nullptr;
  grpc_jwt_claims* claims = nullptr;
  grpc_slice header_buffer;
  grpc_slice claims_buffer;
  grpc_slice signature;
  size_t signed_jwt_len;
  const char* cur = jwt;

  GPR_ASSERT(verifier != nullptr && jwt != nullptr && audience != nullptr &&
             cb != nullptr);
  dot = strchr(cur, '.');
  if (dot == nullptr) goto error;
  json = parse_json_part_from_jwt(exec_ctx, cur, (size_t)(dot - cur),
                                  &header_buffer);
  if (json == nullptr) goto error;
  header = jose_header_from_json(exec_ctx, json, header_buffer);
  if (header == nullptr) goto error;

  cur = dot + 1;
  dot = strchr(cur, '.');
  if (dot == nullptr) goto error;
  json = parse_json_part_from_jwt(exec_ctx, cur, (size_t)(dot - cur),
                                  &claims_buffer);
  if (json == nullptr) goto error;
  claims = grpc_jwt_claims_from_json(exec_ctx, json, claims_buffer);
  if (claims == nullptr) goto error;

  signed_jwt_len = (size_t)(dot - jwt);
  cur = dot + 1;
  signature = grpc_base64_decode(exec_ctx, cur, 1);
  if (GRPC_SLICE_IS_EMPTY(signature)) goto error;
  retrieve_key_and_verify(
      exec_ctx,
      verifier_cb_ctx_create(verifier, pollset, header, claims, audience,
                             signature, jwt, signed_jwt_len, user_data, cb));
  return;

error:
  if (header != nullptr) jose_header_destroy(exec_ctx, header);
  if (claims != nullptr) grpc_jwt_claims_destroy(exec_ctx, claims);
  cb(exec_ctx, user_data, GRPC_JWT_VERIFIER_BAD_FORMAT, nullptr);
}

grpc_jwt_verifier* grpc_jwt_verifier_create(
    const grpc_jwt_verifier_email_domain_key_url_mapping* mappings,
    size_t num_mappings) {
  grpc_jwt_verifier* v =
      (grpc_jwt_verifier*)gpr_zalloc(sizeof(grpc_jwt_verifier));
  grpc_httpcli_context_init(&v->http_ctx);

  /* We know at least of one mapping. */
  v->allocated_mappings = 1 + num_mappings;
  v->mappings = (email_key_mapping*)gpr_malloc(v->allocated_mappings *
                                               sizeof(email_key_mapping));
  verifier_put_mapping(v, GRPC_GOOGLE_SERVICE_ACCOUNTS_EMAIL_DOMAIN,
                       GRPC_GOOGLE_SERVICE_ACCOUNTS_KEY_URL_PREFIX);
  /* User-Provided mappings. */
  if (mappings != nullptr) {
    size_t i;
    for (i = 0; i < num_mappings; i++) {
      verifier_put_mapping(v, mappings[i].email_domain,
                           mappings[i].key_url_prefix);
    }
  }
  return v;
}

void grpc_jwt_verifier_destroy(grpc_exec_ctx* exec_ctx, grpc_jwt_verifier* v) {
  size_t i;
  if (v == nullptr) return;
  grpc_httpcli_context_destroy(exec_ctx, &v->http_ctx);
  if (v->mappings != nullptr) {
    for (i = 0; i < v->num_mappings; i++) {
      gpr_free(v->mappings[i].email_domain);
      gpr_free(v->mappings[i].key_url_prefix);
    }
    gpr_free(v->mappings);
  }
  gpr_free(v);
}
