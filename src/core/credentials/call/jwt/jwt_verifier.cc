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

#include "src/core/credentials/call/jwt/jwt_verifier.h"

#include <grpc/support/port_platform.h>
#include <limits.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/param_build.h>
#endif

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "src/core/credentials/call/call_credentials.h"  // IWYU pragma: keep
#include "src/core/credentials/transport/transport_credentials.h"  // IWYU pragma: keep
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/ssl_types.h"
#include "src/core/util/http_client/httpcli.h"
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/util/http_client/parser.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/manual_constructor.h"
#include "src/core/util/memory.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/string.h"
#include "src/core/util/uri.h"

using grpc_core::Json;

// --- Utils. ---

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

static Json parse_json_part_from_jwt(const char* str, size_t len) {
  std::string string;
  if (!absl::WebSafeBase64Unescape(absl::string_view(str, len), &string)) {
    LOG(ERROR) << "Invalid base64.";
    return Json();  // JSON null
  }
  auto json = grpc_core::JsonParse(string);
  if (!json.ok()) {
    LOG(ERROR) << "JSON parse error: " << json.status();
    return Json();  // JSON null
  }
  return std::move(*json);
}

static const char* validate_string_field(const Json& json, const char* key) {
  if (json.type() != Json::Type::kString) {
    LOG(ERROR) << "Invalid " << key << " field";
    return nullptr;
  }
  return json.string().c_str();
}

static gpr_timespec validate_time_field(const Json& json, const char* key) {
  gpr_timespec result = gpr_time_0(GPR_CLOCK_REALTIME);
  if (json.type() != Json::Type::kNumber) {
    LOG(ERROR) << "Invalid " << key << " field";
    return result;
  }
  result.tv_sec = strtol(json.string().c_str(), nullptr, 10);
  return result;
}

// --- JOSE header. see http://tools.ietf.org/html/rfc7515#section-4 ---

struct jose_header {
  const char* alg;
  const char* kid;
  const char* typ;
  // TODO(jboeuf): Add others as needed (jku, jwk, x5u, x5c and so on...).
  grpc_core::ManualConstructor<Json> json;
};
static void jose_header_destroy(jose_header* h) {
  h->json.Destroy();
  gpr_free(h);
}

static jose_header* jose_header_from_json(Json json) {
  const char* alg_value;
  Json::Object::const_iterator it;
  jose_header* h = grpc_core::Zalloc<jose_header>();
  if (json.type() != Json::Type::kObject) {
    LOG(ERROR) << "JSON value is not an object";
    goto error;
  }
  // Check alg field.
  it = json.object().find("alg");
  if (it == json.object().end()) {
    LOG(ERROR) << "Missing alg field.";
    goto error;
  }
  // We only support RSA-1.5 signatures for now.
  // Beware of this if we add HMAC support:
  // https://auth0.com/blog/2015/03/31/critical-vulnerabilities-in-json-web-token-libraries/
  //
  alg_value = it->second.string().c_str();
  if (it->second.type() != Json::Type::kString ||
      strncmp(alg_value, "RS", 2) != 0 ||
      evp_md_from_alg(alg_value) == nullptr) {
    LOG(ERROR) << "Invalid alg field";
    goto error;
  }
  h->alg = alg_value;
  // Check typ field.
  it = json.object().find("typ");
  if (it != json.object().end()) {
    h->typ = validate_string_field(it->second, "typ");
    if (h->typ == nullptr) goto error;
  }
  // Check kid field.
  it = json.object().find("kid");
  if (it != json.object().end()) {
    h->kid = validate_string_field(it->second, "kid");
    if (h->kid == nullptr) goto error;
  }
  h->json.Init(std::move(json));
  return h;

error:
  jose_header_destroy(h);
  return nullptr;
}

// --- JWT claims. see http://tools.ietf.org/html/rfc7519#section-4.1

struct grpc_jwt_claims {
  // Well known properties already parsed.
  const char* sub;
  const char* iss;
  const char* aud;
  const char* jti;
  gpr_timespec iat;
  gpr_timespec exp;
  gpr_timespec nbf;

  grpc_core::ManualConstructor<Json> json;
};

void grpc_jwt_claims_destroy(grpc_jwt_claims* claims) {
  claims->json.Destroy();
  gpr_free(claims);
}

const Json* grpc_jwt_claims_json(const grpc_jwt_claims* claims) {
  if (claims == nullptr) return nullptr;
  return claims->json.get();
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

grpc_jwt_claims* grpc_jwt_claims_from_json(Json json) {
  grpc_jwt_claims* claims = grpc_core::Zalloc<grpc_jwt_claims>();
  claims->json.Init(std::move(json));
  claims->iat = gpr_inf_past(GPR_CLOCK_REALTIME);
  claims->nbf = gpr_inf_past(GPR_CLOCK_REALTIME);
  claims->exp = gpr_inf_future(GPR_CLOCK_REALTIME);

  // Per the spec, all fields are optional.
  for (const auto& p : claims->json->object()) {
    if (p.first == "sub") {
      claims->sub = validate_string_field(p.second, "sub");
      if (claims->sub == nullptr) goto error;
    } else if (p.first == "iss") {
      claims->iss = validate_string_field(p.second, "iss");
      if (claims->iss == nullptr) goto error;
    } else if (p.first == "aud") {
      claims->aud = validate_string_field(p.second, "aud");
      if (claims->aud == nullptr) goto error;
    } else if (p.first == "jti") {
      claims->jti = validate_string_field(p.second, "jti");
      if (claims->jti == nullptr) goto error;
    } else if (p.first == "iat") {
      claims->iat = validate_time_field(p.second, "iat");
      if (gpr_time_cmp(claims->iat, gpr_time_0(GPR_CLOCK_REALTIME)) == 0) {
        goto error;
      }
    } else if (p.first == "exp") {
      claims->exp = validate_time_field(p.second, "exp");
      if (gpr_time_cmp(claims->exp, gpr_time_0(GPR_CLOCK_REALTIME)) == 0) {
        goto error;
      }
    } else if (p.first == "nbf") {
      claims->nbf = validate_time_field(p.second, "nbf");
      if (gpr_time_cmp(claims->nbf, gpr_time_0(GPR_CLOCK_REALTIME)) == 0) {
        goto error;
      }
    }
  }
  return claims;

error:
  grpc_jwt_claims_destroy(claims);
  return nullptr;
}

grpc_jwt_verifier_status grpc_jwt_claims_check(const grpc_jwt_claims* claims,
                                               const char* audience) {
  gpr_timespec skewed_now;
  int audience_ok;

  CHECK_NE(claims, nullptr);

  skewed_now =
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), grpc_jwt_verifier_clock_skew);
  if (gpr_time_cmp(skewed_now, claims->nbf) < 0) {
    LOG(ERROR) << "JWT is not valid yet.";
    return GRPC_JWT_VERIFIER_TIME_CONSTRAINT_FAILURE;
  }
  skewed_now =
      gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), grpc_jwt_verifier_clock_skew);
  if (gpr_time_cmp(skewed_now, claims->exp) > 0) {
    LOG(ERROR) << "JWT is expired.";
    return GRPC_JWT_VERIFIER_TIME_CONSTRAINT_FAILURE;
  }

  // This should be probably up to the upper layer to decide but let's hardcode
  // the 99% use case here for email issuers, where the JWT must be self
  // issued.
  if (grpc_jwt_issuer_email_domain(claims->iss) != nullptr &&
      claims->sub != nullptr && strcmp(claims->iss, claims->sub) != 0) {
    LOG(ERROR) << "Email issuer (" << claims->iss
               << ") cannot assert another subject (" << claims->sub
               << ") than itself.";
    return GRPC_JWT_VERIFIER_BAD_SUBJECT;
  }

  if (audience == nullptr) {
    audience_ok = claims->aud == nullptr;
  } else {
    audience_ok = claims->aud != nullptr && strcmp(audience, claims->aud) == 0;
  }
  if (!audience_ok) {
    LOG(ERROR) << "Audience mismatch: expected "
               << (audience == nullptr ? "NULL" : audience) << " and found "
               << (claims->aud == nullptr ? "NULL" : claims->aud);
    return GRPC_JWT_VERIFIER_BAD_AUDIENCE;
  }
  return GRPC_JWT_VERIFIER_OK;
}

// --- verifier_cb_ctx object. ---

typedef enum {
  HTTP_RESPONSE_OPENID = 0,
  HTTP_RESPONSE_KEYS,
  HTTP_RESPONSE_COUNT  // must be last
} http_response_index;

struct verifier_cb_ctx {
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
  grpc_core::OrphanablePtr<grpc_core::HttpRequest> http_request;
};
// Takes ownership of the header, claims and signature.
static verifier_cb_ctx* verifier_cb_ctx_create(
    grpc_jwt_verifier* verifier, grpc_pollset* pollset, jose_header* header,
    grpc_jwt_claims* claims, const char* audience, const grpc_slice& signature,
    const char* signed_jwt, size_t signed_jwt_len, void* user_data,
    grpc_jwt_verification_done_cb cb) {
  grpc_core::ExecCtx exec_ctx;
  verifier_cb_ctx* ctx = new verifier_cb_ctx();
  ctx->verifier = verifier;
  ctx->pollent = grpc_polling_entity_create_from_pollset(pollset);
  ctx->header = header;
  ctx->audience = gpr_strdup(audience);
  ctx->claims = claims;
  ctx->signature = signature;
  ctx->signed_data = grpc_slice_from_copied_buffer(signed_jwt, signed_jwt_len);
  ctx->user_data = user_data;
  ctx->user_cb = cb;
  return ctx;
}

void verifier_cb_ctx_destroy(verifier_cb_ctx* ctx) {
  if (ctx->audience != nullptr) gpr_free(ctx->audience);
  if (ctx->claims != nullptr) grpc_jwt_claims_destroy(ctx->claims);
  grpc_core::CSliceUnref(ctx->signature);
  grpc_core::CSliceUnref(ctx->signed_data);
  jose_header_destroy(ctx->header);
  for (size_t i = 0; i < HTTP_RESPONSE_COUNT; i++) {
    grpc_http_response_destroy(&ctx->responses[i]);
  }
  // TODO(unknown): see what to do with claims...
  delete ctx;
}

// --- grpc_jwt_verifier object. ---

// Clock skew defaults to one minute.
gpr_timespec grpc_jwt_verifier_clock_skew = {60, 0, GPR_TIMESPAN};

// Max delay defaults to one minute.
grpc_core::Duration grpc_jwt_verifier_max_delay =
    grpc_core::Duration::Minutes(1);

struct email_key_mapping {
  char* email_domain;
  char* key_url_prefix;
};
struct grpc_jwt_verifier {
  email_key_mapping* mappings;
  size_t num_mappings;  // Should be very few, linear search ok.
  size_t allocated_mappings;
};

static Json json_from_http(const grpc_http_response* response) {
  if (response == nullptr) {
    LOG(ERROR) << "HTTP response is NULL.";
    return Json();  // JSON null
  }
  if (response->status != 200) {
    LOG(ERROR) << "Call to http server failed with error " << response->status;
    return Json();  // JSON null
  }
  auto json = grpc_core::JsonParse(
      absl::string_view(response->body, response->body_length));
  if (!json.ok()) {
    LOG(ERROR) << "Invalid JSON found in response.";
    return Json();  // JSON null
  }
  return std::move(*json);
}

static const Json* find_property_by_name(const Json& json, const char* name) {
  auto it = json.object().find(name);
  if (it == json.object().end()) {
    return nullptr;
  }
  return &it->second;
}

static EVP_PKEY* extract_pkey_from_x509(const char* x509_str) {
  X509* x509 = nullptr;
  EVP_PKEY* result = nullptr;
  BIO* bio = BIO_new(BIO_s_mem());
  size_t len = strlen(x509_str);
  CHECK_LT(len, static_cast<size_t>(INT_MAX));
  BIO_write(bio, x509_str, static_cast<int>(len));
  x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  if (x509 == nullptr) {
    LOG(ERROR) << "Unable to parse x509 cert.";
    goto end;
  }
  result = X509_get_pubkey(x509);
  if (result == nullptr) {
    LOG(ERROR) << "Cannot find public key in X509 cert.";
  }

end:
  BIO_free(bio);
  X509_free(x509);
  return result;
}

static BIGNUM* bignum_from_base64(const char* b64) {
  if (b64 == nullptr) return nullptr;
  std::string string;
  if (!absl::WebSafeBase64Unescape(b64, &string)) {
    LOG(ERROR) << "Invalid base64 for big num.";
    return nullptr;
  }
  return BN_bin2bn(reinterpret_cast<const uint8_t*>(string.data()),
                   TSI_SIZE_AS_SIZE(string.size()), nullptr);
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L

// Provide compatibility across OpenSSL 1.02 and 1.1.
static int RSA_set0_key(RSA* r, BIGNUM* n, BIGNUM* e, BIGNUM* d) {
  // If the fields n and e in r are NULL, the corresponding input
  // parameters MUST be non-NULL for n and e.  d may be
  // left NULL (in case only the public key is used).
  //
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

static EVP_PKEY* pkey_from_jwk(const Json& json, const char* kty) {
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  RSA* rsa = nullptr;
#else
  EVP_PKEY_CTX* ctx = nullptr;
  OSSL_PARAM* params = NULL;
  OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
#endif
  EVP_PKEY* result = nullptr;
  BIGNUM* tmp_n = nullptr;
  BIGNUM* tmp_e = nullptr;
  Json::Object::const_iterator it;

  CHECK(json.type() == Json::Type::kObject);
  CHECK_NE(kty, nullptr);
  if (strcmp(kty, "RSA") != 0) {
    LOG(ERROR) << "Unsupported key type " << kty;
    goto end;
  }
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  rsa = RSA_new();
  if (rsa == nullptr) {
    LOG(ERROR) << "Could not create rsa key.";
    goto end;
  }
#endif
  it = json.object().find("n");
  if (it == json.object().end()) {
    LOG(ERROR) << "Missing RSA public key field.";
    goto end;
  }
  tmp_n = bignum_from_base64(validate_string_field(it->second, "n"));
  if (tmp_n == nullptr) goto end;
  it = json.object().find("e");
  if (it == json.object().end()) {
    LOG(ERROR) << "Missing RSA public key field.";
    goto end;
  }
  tmp_e = bignum_from_base64(validate_string_field(it->second, "e"));
  if (tmp_e == nullptr) goto end;
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  if (!RSA_set0_key(rsa, tmp_n, tmp_e, nullptr)) {
    LOG(ERROR) << "Cannot set RSA key from inputs.";
    goto end;
  }
  // RSA_set0_key takes ownership on success.
  tmp_n = nullptr;
  tmp_e = nullptr;
  result = EVP_PKEY_new();
  EVP_PKEY_set1_RSA(result, rsa);  // uprefs rsa.
#else

  if (!OSSL_PARAM_BLD_push_BN(bld, "n", tmp_n) ||
      !OSSL_PARAM_BLD_push_BN(bld, "e", tmp_e) ||
      (params = OSSL_PARAM_BLD_to_param(bld)) == NULL) {
    LOG(ERROR) << "Could not create OSSL_PARAM";
    goto end;
  }

  ctx = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
  if (ctx == nullptr) {
    LOG(ERROR) << "Could not create rsa key.";
    goto end;
  }
  if (EVP_PKEY_fromdata_init(ctx) <= 0) {
    LOG(ERROR) << "Could not create rsa key.";
    goto end;
  }
  if (EVP_PKEY_fromdata(ctx, &result, EVP_PKEY_KEYPAIR, params) <= 0) {
    LOG(ERROR) << "Cannot set RSA key from inputs.";
    goto end;
  }
#endif

end:
#if OPENSSL_VERSION_NUMBER < 0x30000000L
  RSA_free(rsa);
#else
  EVP_PKEY_CTX_free(ctx);
  OSSL_PARAM_free(params);
  OSSL_PARAM_BLD_free(bld);
#endif
  BN_free(tmp_n);
  BN_free(tmp_e);
  return result;
}

static EVP_PKEY* find_verification_key(const Json& json, const char* header_alg,
                                       const char* header_kid) {
  // Try to parse the json as a JWK set:
  // https://tools.ietf.org/html/rfc7517#section-5.
  const Json* jwt_keys = find_property_by_name(json, "keys");
  if (jwt_keys == nullptr) {
    // Use the google proprietary format which is:
    // { <kid1>: <x5091>, <kid2>: <x5092>, ... }
    const Json* cur = find_property_by_name(json, header_kid);
    if (cur == nullptr) return nullptr;
    return extract_pkey_from_x509(cur->string().c_str());
  }
  if (jwt_keys->type() != Json::Type::kArray) {
    LOG(ERROR) << "Unexpected value type of keys property in jwks key set.";
    return nullptr;
  }
  // Key format is specified in:
  // https://tools.ietf.org/html/rfc7518#section-6.
  for (const Json& jkey : jwt_keys->array()) {
    if (jkey.type() != Json::Type::kObject) continue;
    const char* alg = nullptr;
    auto it = jkey.object().find("alg");
    if (it != jkey.object().end()) {
      alg = validate_string_field(it->second, "alg");
    }
    const char* kid = nullptr;
    it = jkey.object().find("kid");
    if (it != jkey.object().end()) {
      kid = validate_string_field(it->second, "kid");
    }
    const char* kty = nullptr;
    it = jkey.object().find("kty");
    if (it != jkey.object().end()) {
      kty = validate_string_field(it->second, "kty");
    }
    if (alg != nullptr && kid != nullptr && kty != nullptr &&
        strcmp(kid, header_kid) == 0 && strcmp(alg, header_alg) == 0) {
      return pkey_from_jwk(jkey, kty);
    }
  }
  LOG(ERROR) << "Could not find matching key in key set for kid=" << header_kid
             << " and alg=" << header_alg;
  return nullptr;
}

static int verify_jwt_signature(EVP_PKEY* key, const char* alg,
                                const grpc_slice& signature,
                                const grpc_slice& signed_data) {
  EVP_MD_CTX* md_ctx = EVP_MD_CTX_create();
  const EVP_MD* md = evp_md_from_alg(alg);
  int result = 0;

  CHECK_NE(md, nullptr);  // Checked before.
  if (md_ctx == nullptr) {
    LOG(ERROR) << "Could not create EVP_MD_CTX.";
    goto end;
  }
  if (EVP_DigestVerifyInit(md_ctx, nullptr, md, nullptr, key) != 1) {
    LOG(ERROR) << "EVP_DigestVerifyInit failed.";
    goto end;
  }
  if (EVP_DigestVerifyUpdate(md_ctx, GRPC_SLICE_START_PTR(signed_data),
                             GRPC_SLICE_LENGTH(signed_data)) != 1) {
    LOG(ERROR) << "EVP_DigestVerifyUpdate failed.";
    goto end;
  }
  if (EVP_DigestVerifyFinal(md_ctx, GRPC_SLICE_START_PTR(signature),
                            GRPC_SLICE_LENGTH(signature)) != 1) {
    LOG(ERROR) << "JWT signature verification failed.";

    goto end;
  }
  result = 1;

end:
  EVP_MD_CTX_destroy(md_ctx);
  return result;
}

static void on_keys_retrieved(void* user_data, grpc_error_handle /*error*/) {
  verifier_cb_ctx* ctx = static_cast<verifier_cb_ctx*>(user_data);
  Json json = json_from_http(&ctx->responses[HTTP_RESPONSE_KEYS]);
  EVP_PKEY* verification_key = nullptr;
  grpc_jwt_verifier_status status = GRPC_JWT_VERIFIER_GENERIC_ERROR;
  grpc_jwt_claims* claims = nullptr;

  if (json.type() == Json::Type::kNull) {
    status = GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR;
    goto end;
  }
  verification_key =
      find_verification_key(json, ctx->header->alg, ctx->header->kid);
  if (verification_key == nullptr) {
    LOG(ERROR) << "Could not find verification key with kid "
               << ctx->header->kid;
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
    // Pass ownership.
    claims = ctx->claims;
    ctx->claims = nullptr;
  }

end:
  EVP_PKEY_free(verification_key);
  ctx->user_cb(ctx->user_data, status, claims);
  verifier_cb_ctx_destroy(ctx);
}

static void on_openid_config_retrieved(void* user_data,
                                       grpc_error_handle /*error*/) {
  verifier_cb_ctx* ctx = static_cast<verifier_cb_ctx*>(user_data);
  const grpc_http_response* response = &ctx->responses[HTTP_RESPONSE_OPENID];
  Json json = json_from_http(response);
  grpc_http_request req;
  memset(&req, 0, sizeof(grpc_http_request));
  const char* jwks_uri;
  const Json* cur;
  absl::StatusOr<grpc_core::URI> uri;
  char* host;
  char* path;

  // TODO(jboeuf): Cache the jwks_uri in order to avoid this hop next time.
  if (json.type() == Json::Type::kNull) goto error;
  cur = find_property_by_name(json, "jwks_uri");
  if (cur == nullptr) {
    LOG(ERROR) << "Could not find jwks_uri in openid config.";
    goto error;
  }
  jwks_uri = validate_string_field(*cur, "jwks_uri");
  if (jwks_uri == nullptr) goto error;
  if (strstr(jwks_uri, "https://") != jwks_uri) {
    LOG(ERROR) << "Invalid non https jwks_uri: " << jwks_uri;
    goto error;
  }
  jwks_uri += 8;
  host = gpr_strdup(jwks_uri);
  path = const_cast<char*>(strchr(jwks_uri, '/'));
  if (path == nullptr) {
    path = const_cast<char*>("");
  } else {
    *(host + (path - jwks_uri)) = '\0';
  }

  // TODO(ctiller): Carry the resource_quota in ctx and share it with the host
  // channel. This would allow us to cancel an authentication query when under
  // extreme memory pressure.
  uri = grpc_core::URI::Create("https", host, path, {} /* query params /*/,
                               "" /* fragment */);
  if (!uri.ok()) {
    goto error;
  }
  ctx->http_request = grpc_core::HttpRequest::Get(
      std::move(*uri), nullptr /* channel args */, &ctx->pollent, &req,
      grpc_core::Timestamp::Now() + grpc_jwt_verifier_max_delay,
      GRPC_CLOSURE_CREATE(on_keys_retrieved, ctx, grpc_schedule_on_exec_ctx),
      &ctx->responses[HTTP_RESPONSE_KEYS],
      grpc_core::CreateHttpRequestSSLCredentials());
  ctx->http_request->Start();
  gpr_free(host);
  return;

error:
  ctx->user_cb(ctx->user_data, GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR, nullptr);
  verifier_cb_ctx_destroy(ctx);
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
  CHECK(v->num_mappings < v->allocated_mappings);
  if (mapping != nullptr) {
    gpr_free(mapping->key_url_prefix);
    mapping->key_url_prefix = gpr_strdup(key_url_prefix);
    return;
  }
  v->mappings[v->num_mappings].email_domain = gpr_strdup(email_domain);
  v->mappings[v->num_mappings].key_url_prefix = gpr_strdup(key_url_prefix);
  v->num_mappings++;
  CHECK(v->num_mappings <= v->allocated_mappings);
}

// Very non-sophisticated way to detect an email address. Should be good
// enough for now...
const char* grpc_jwt_issuer_email_domain(const char* issuer) {
  const char* at_sign = strchr(issuer, '@');
  if (at_sign == nullptr) return nullptr;
  const char* email_domain = at_sign + 1;
  if (*email_domain == '\0') return nullptr;
  const char* dot = strrchr(email_domain, '.');
  if (dot == nullptr || dot == email_domain) return email_domain;
  CHECK(dot > email_domain);
  // There may be a subdomain, we just want the domain.
  dot = static_cast<const char*>(
      gpr_memrchr(email_domain, '.', static_cast<size_t>(dot - email_domain)));
  if (dot == nullptr) return email_domain;
  return dot + 1;
}

// Takes ownership of ctx.
static void retrieve_key_and_verify(verifier_cb_ctx* ctx) {
  const char* email_domain;
  grpc_closure* http_cb;
  char* path_prefix = nullptr;
  const char* iss;
  grpc_http_request req;
  memset(&req, 0, sizeof(grpc_http_request));
  http_response_index rsp_idx;
  char* host;
  char* path;
  absl::StatusOr<grpc_core::URI> uri;

  CHECK(ctx != nullptr && ctx->header != nullptr && ctx->claims != nullptr);
  iss = ctx->claims->iss;
  if (ctx->header->kid == nullptr) {
    LOG(ERROR) << "Missing kid in jose header.";
    goto error;
  }
  if (iss == nullptr) {
    LOG(ERROR) << "Missing iss in claims.";
    goto error;
  }

  // This code relies on:
  // https://openid.net/specs/openid-connect-discovery-1_0.html
  // Nobody seems to implement the account/email/webfinger part 2. of the spec
  // so we will rely instead on email/url mappings if we detect such an issuer.
  // Part 4, on the other hand is implemented by both google and salesforce.
  email_domain = grpc_jwt_issuer_email_domain(iss);
  if (email_domain != nullptr) {
    email_key_mapping* mapping;
    CHECK_NE(ctx->verifier, nullptr);
    mapping = verifier_get_mapping(ctx->verifier, email_domain);
    if (mapping == nullptr) {
      LOG(ERROR) << "Missing mapping for issuer email.";
      goto error;
    }
    host = gpr_strdup(mapping->key_url_prefix);
    path_prefix = strchr(host, '/');
    if (path_prefix == nullptr) {
      gpr_asprintf(&path, "/%s", iss);
    } else {
      *(path_prefix++) = '\0';
      gpr_asprintf(&path, "/%s/%s", path_prefix, iss);
    }
    http_cb =
        GRPC_CLOSURE_CREATE(on_keys_retrieved, ctx, grpc_schedule_on_exec_ctx);
    rsp_idx = HTTP_RESPONSE_KEYS;
  } else {
    host = gpr_strdup(strstr(iss, "https://") == iss ? iss + 8 : iss);
    path_prefix = strchr(host, '/');
    if (path_prefix == nullptr) {
      path = gpr_strdup(GRPC_OPENID_CONFIG_URL_SUFFIX);
    } else {
      *(path_prefix++) = 0;
      gpr_asprintf(&path, "/%s%s", path_prefix, GRPC_OPENID_CONFIG_URL_SUFFIX);
    }
    http_cb = GRPC_CLOSURE_CREATE(on_openid_config_retrieved, ctx,
                                  grpc_schedule_on_exec_ctx);
    rsp_idx = HTTP_RESPONSE_OPENID;
  }

  // TODO(ctiller): Carry the resource_quota in ctx and share it with the host
  // channel. This would allow us to cancel an authentication query when under
  // extreme memory pressure.
  uri = grpc_core::URI::Create("https", host, path, {} /* query params */,
                               "" /* fragment */);
  if (!uri.ok()) {
    goto error;
  }
  ctx->http_request = grpc_core::HttpRequest::Get(
      std::move(*uri), nullptr /* channel args */, &ctx->pollent, &req,
      grpc_core::Timestamp::Now() + grpc_jwt_verifier_max_delay, http_cb,
      &ctx->responses[rsp_idx], grpc_core::CreateHttpRequestSSLCredentials());
  ctx->http_request->Start();
  gpr_free(host);
  gpr_free(path);
  return;

error:
  ctx->user_cb(ctx->user_data, GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR, nullptr);
  verifier_cb_ctx_destroy(ctx);
}

void grpc_jwt_verifier_verify(grpc_jwt_verifier* verifier,
                              grpc_pollset* pollset, const char* jwt,
                              const char* audience,
                              grpc_jwt_verification_done_cb cb,
                              void* user_data) {
  const char* dot = nullptr;
  jose_header* header = nullptr;
  grpc_jwt_claims* claims = nullptr;
  grpc_slice signature;
  size_t signed_jwt_len;
  const char* cur = jwt;
  Json json;
  std::string signature_str;

  CHECK(verifier != nullptr && jwt != nullptr && audience != nullptr &&
        cb != nullptr);
  dot = strchr(cur, '.');
  if (dot == nullptr) goto error;
  json = parse_json_part_from_jwt(cur, static_cast<size_t>(dot - cur));
  if (json.type() == Json::Type::kNull) goto error;
  header = jose_header_from_json(std::move(json));
  if (header == nullptr) goto error;

  cur = dot + 1;
  dot = strchr(cur, '.');
  if (dot == nullptr) goto error;
  json = parse_json_part_from_jwt(cur, static_cast<size_t>(dot - cur));
  if (json.type() == Json::Type::kNull) goto error;
  claims = grpc_jwt_claims_from_json(std::move(json));
  if (claims == nullptr) goto error;

  signed_jwt_len = static_cast<size_t>(dot - jwt);
  cur = dot + 1;

  if (!absl::WebSafeBase64Unescape(cur, &signature_str)) goto error;
  signature = grpc_slice_from_cpp_string(std::move(signature_str));
  retrieve_key_and_verify(
      verifier_cb_ctx_create(verifier, pollset, header, claims, audience,
                             signature, jwt, signed_jwt_len, user_data, cb));
  return;

error:
  if (header != nullptr) jose_header_destroy(header);
  if (claims != nullptr) grpc_jwt_claims_destroy(claims);
  cb(user_data, GRPC_JWT_VERIFIER_BAD_FORMAT, nullptr);
}

grpc_jwt_verifier* grpc_jwt_verifier_create(
    const grpc_jwt_verifier_email_domain_key_url_mapping* mappings,
    size_t num_mappings) {
  grpc_jwt_verifier* v = grpc_core::Zalloc<grpc_jwt_verifier>();

  // We know at least of one mapping.
  v->allocated_mappings = 1 + num_mappings;
  v->mappings = static_cast<email_key_mapping*>(
      gpr_malloc(v->allocated_mappings * sizeof(email_key_mapping)));
  verifier_put_mapping(v, GRPC_GOOGLE_SERVICE_ACCOUNTS_EMAIL_DOMAIN,
                       GRPC_GOOGLE_SERVICE_ACCOUNTS_KEY_URL_PREFIX);
  // User-Provided mappings.
  if (mappings != nullptr) {
    size_t i;
    for (i = 0; i < num_mappings; i++) {
      verifier_put_mapping(v, mappings[i].email_domain,
                           mappings[i].key_url_prefix);
    }
  }
  return v;
}

void grpc_jwt_verifier_destroy(grpc_jwt_verifier* v) {
  size_t i;
  if (v == nullptr) return;
  if (v->mappings != nullptr) {
    for (i = 0; i < v->num_mappings; i++) {
      gpr_free(v->mappings[i].email_domain);
      gpr_free(v->mappings[i].key_url_prefix);
    }
    gpr_free(v->mappings);
  }
  gpr_free(v);
}
