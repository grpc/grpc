//
// Copyright 2026 gRPC authors.
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

#include "src/core/credentials/call/gdch_service_account/gdch_service_account_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <string.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/util/http_client/httpcli.h"
#include "src/core/util/http_client/httpcli_ssl_credentials.h"
#include "src/core/util/http_client/parser.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/string.h"
#include "src/core/util/time.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/uri.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

constexpr const char* kExpectedType = "gdch_service_account";
constexpr const char* kExpectedFormatVersion = "1";
constexpr std::chrono::seconds kTokenLifetime = std::chrono::seconds(3600);

struct OpenSslDeleter {
  void operator()(EVP_MD_CTX* ptr) {
    // The name of the function to free an EVP_MD_CTX changed in OpenSSL 1.1.0.
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)  // Older than version 1.1.0.
    EVP_MD_CTX_destroy(ptr);
#else
    EVP_MD_CTX_free(ptr);
#endif
  }

  void operator()(EVP_PKEY* ptr) { EVP_PKEY_free(ptr); }
  void operator()(BIO* ptr) { BIO_free(ptr); }
  void operator()(ECDSA_SIG* ptr) { ECDSA_SIG_free(ptr); }
};

std::unique_ptr<EVP_MD_CTX, OpenSslDeleter> GetDigestCtx() {
  // The name of the function to create an EVP_MD_CTX changed in OpenSSL 1.1.0.
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)  // Older than version 1.1.0.
  return std::unique_ptr<EVP_MD_CTX, OpenSslDeleter>(EVP_MD_CTX_create());
#else
  return std::unique_ptr<EVP_MD_CTX, OpenSslDeleter>(EVP_MD_CTX_new());
#endif
}

std::string CaptureSslErrors() {
  std::string msg;
  absl::string_view sep = "";
  while (unsigned long code = ERR_get_error()) {
    // OpenSSL guarantees that 256 bytes is enough:
    //   https://www.openssl.org/docs/man1.1.1/man3/ERR_error_string_n.html
    //   https://www.openssl.org/docs/man1.0.2/man3/ERR_error_string_n.html
    // we could not find a macro or constant to replace the 256 literal.
    constexpr size_t kMaxOpenSslErrorLength = 256;
    std::array<char, kMaxOpenSslErrorLength> buf{};
    ERR_error_string_n(code, buf.data(), buf.size());
    StrAppend(msg, sep);
    StrAppend(msg, buf.data());
    sep = ", ";
  }
  return msg;
}

absl::Status DERToRawSignature(const unsigned char* der_sig, size_t der_len,
                               int coord_size, std::vector<uint8_t>& raw_sig) {
  if (der_sig == nullptr || der_len == 0) {
    return absl::InternalError("Input DER signature is empty");
  }

  std::unique_ptr<ECDSA_SIG, OpenSslDeleter> ecdsa_sig(
      d2i_ECDSA_SIG(nullptr, &der_sig, der_len));

  if (ecdsa_sig == nullptr) {
    char err_buf[256];
    ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
    return absl::InternalError(
        absl::StrCat("Error parsing DER signature: ", err_buf));
  }

  const BIGNUM* r;
  const BIGNUM* s;
  ECDSA_SIG_get0(ecdsa_sig.get(), &r, &s);

  if (r == nullptr || s == nullptr) {
    return absl::InternalError("Error: Could not get r or s from ECDSA_SIG");
  }

  raw_sig.resize(2 * coord_size);
  unsigned char* raw_sig_ptr = raw_sig.data();

  constexpr const char* kErrorMessage =
      R"""(Error converting %s to binary (expected %d bytes, got %d): %s)""";
  // Convert r to binary, padded to coord_size.
  int r_len = BN_bn2binpad(r, &raw_sig_ptr[0], coord_size);
  if (r_len != coord_size) {
    char err_buf[256];
    ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
    std::string err_msg =
        absl::StrFormat(kErrorMessage, "r", coord_size, r_len, err_buf);
    return absl::InternalError(err_msg);
  }

  // Convert s to binary, padded to coord_size.
  int s_len = BN_bn2binpad(s, &raw_sig_ptr[coord_size], coord_size);
  if (s_len != coord_size) {
    char err_buf[256];
    ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
    std::string err_msg =
        absl::StrFormat(kErrorMessage, "s", coord_size, s_len, err_buf);
    return absl::InternalError(err_msg);
  }

  return {};
}

}  // namespace

absl::StatusOr<std::vector<std::uint8_t>>
GDCHServiceAccountCredentials::SignUsingSha256(const std::string& str,
                                               const std::string& pem_contents,
                                               SignatureFormat format) {
  ERR_clear_error();
  std::unique_ptr<BIO, OpenSslDeleter> pem_buffer(BIO_new_mem_buf(
      pem_contents.data(), static_cast<int>(pem_contents.length())));
  if (pem_buffer == nullptr) {
    return absl::InternalError(absl::StrCat(
        "Invalid ServiceAccountCredentials could not create PEM buffer: ",
        CaptureSslErrors()));
  }

  std::unique_ptr<EVP_PKEY, OpenSslDeleter> private_key(PEM_read_bio_PrivateKey(
      pem_buffer.get(),
      nullptr,  // EVP_PKEY **x
      nullptr,  // pem_password_cb *cb -- a custom callback.
      // void *u -- this represents the password for the PEM (only
      // applicable for formats such as PKCS12 (.p12 files) that use
      // a password, which we don't currently support.
      nullptr));
  if (private_key == nullptr) {
    return absl::InternalError(
        absl::StrCat("Invalid ServiceAccountCredentials could not parse PEM to "
                     "get private key: ",
                     CaptureSslErrors()));
  }

  std::unique_ptr<EVP_MD_CTX, OpenSslDeleter> digest_ctx = GetDigestCtx();
  if (digest_ctx == nullptr) {
    return absl::InternalError(
        absl::StrCat("Invalid ServiceAccountCredentials could not create "
                     "context for OpenSSL digest: ",
                     CaptureSslErrors()));
  }

  constexpr int kOpenSslSuccess = 1;
  if (EVP_DigestSignInit(digest_ctx.get(), nullptr, EVP_sha256(), nullptr,
                         private_key.get()) != kOpenSslSuccess) {
    return absl::InternalError(
        absl::StrCat("Invalid ServiceAccountCredentials - could not initialize "
                     "signing digest: ",
                     CaptureSslErrors()));
  }

  if (EVP_DigestSignUpdate(digest_ctx.get(), str.data(), str.size()) !=
      kOpenSslSuccess) {
    return absl::InternalError(absl::StrCat(
        "Invalid ServiceAccountCredentials - could not sign blob: ",
        CaptureSslErrors()));
  }

  // The signed SHA256 size depends on the size (the experts say "modulus") of
  // they key.  First query the size:
  std::size_t actual_len = 0;
  if (EVP_DigestSignFinal(digest_ctx.get(), nullptr, &actual_len) !=
      kOpenSslSuccess) {
    return absl::InternalError(absl::StrCat(
        "Invalid ServiceAccountCredentials - could not sign blob: ",
        CaptureSslErrors()));
  }

  // Then compute the actual signed digest. Note that OpenSSL requires a
  // `unsigned char*` buffer, so we feed it that.
  std::vector<unsigned char> buffer(actual_len);
  if (EVP_DigestSignFinal(digest_ctx.get(), buffer.data(), &actual_len) !=
      kOpenSslSuccess) {
    return absl::InternalError(absl::StrCat(
        "Invalid ServiceAccountCredentials - could not sign blob: ",
        CaptureSslErrors()));
  }

  std::vector<std::uint8_t> der_sig{buffer.begin(),
                                    std::next(buffer.begin(), actual_len)};
  if (format == SignatureFormat::kDER) {
    return der_sig;
  }

  std::vector<std::uint8_t> raw_sig;
  absl::Status status =
      DERToRawSignature(der_sig.data(), der_sig.size(), 32, raw_sig);
  if (!status.ok()) return status;
  return raw_sig;
}

absl::StatusOr<GDCHServiceAccountCredentials::Info>
GDCHServiceAccountCredentials::ParseServiceAccountJson(const Json& json) {
  if (json.type() != Json::Type::kObject) {
    return absl::InternalError("Invalid json to construct credentials info.");
  }
  using iterator_type = Json::Object::const_iterator;
  using Validator =
      std::function<absl::Status(absl::string_view name, const iterator_type&,
                                 std::optional<std::string>)>;
  using Store = std::function<void(Info&, const iterator_type&)>;

  Validator optional_field =
      [&](absl::string_view name, const iterator_type& l,
          const std::optional<std::string>&) -> absl::Status {
    if (l == json.object().end()) return absl::OkStatus();
    if (l->second.string().empty()) {
      return absl::InternalError(
          absl::StrCat(name, " field must not be empty"));
    }
    return absl::OkStatus();
  };
  Validator required_field =
      [&](absl::string_view name, const iterator_type& l,
          const std::optional<std::string>& value) -> absl::Status {
    if (l == json.object().end()) {
      return absl::InternalError(absl::StrCat(name, " field not present"));
    }
    if (l->second.string().empty()) {
      return absl::InternalError(
          absl::StrCat(name, " field must not be empty"));
    }
    if (value.has_value() && l->second.string() != *value) {
      return absl::InternalError(absl::StrCat(name, " field must be ", *value));
    }
    return absl::OkStatus();
  };

  struct Field {
    std::string name;
    Validator validator;
    Store store;
    std::optional<std::string> value = std::nullopt;
  };
  std::vector<Field> fields{
      {"type", required_field,
       [](Info& info, const iterator_type& l) {
         info.type = l->second.string();
       },
       kExpectedType},
      {"format_version", required_field,
       [](Info& info, const iterator_type& l) {
         info.format_version = l->second.string();
       },
       kExpectedFormatVersion},
      {"project", required_field,
       [](Info& info, const iterator_type& l) {
         info.project_id = l->second.string();
       }},
      {"private_key_id", required_field,
       [&](Info& info, const iterator_type& l) {
         info.private_key_id = l->second.string();
       }},
      {"private_key", required_field,
       [](Info& info, const iterator_type& l) {
         info.private_key = l->second.string();
       }},
      {"name", required_field,
       [&](Info& info, const iterator_type& l) {
         info.service_identity_name = l->second.string();
       }},
      {"ca_cert_path", optional_field,
       [&](Info& info, const iterator_type& l) {
         if (l == json.object().end()) return;
         info.ca_cert_path = l->second.string();
       }},
      {"token_uri", required_field, [&](Info& info, const iterator_type& l) {
         info.token_uri = l->second.string();
       }}};

  Info info;
  for (Field& f : fields) {
    Json::Object::const_iterator l = json.object().find(f.name);
    if (l != json.object().end() && l->second.type() != Json::Type::kString) {
      return absl::InternalError(
          absl::StrCat(f.name, " field must be a string"));
    }
    absl::Status status = f.validator(f.name, l, f.value);
    if (!status.ok()) return status;
    f.store(info, l);
  }

  return info;
}

absl::StatusOr<RefCountedPtr<GDCHServiceAccountCredentials>>
GDCHServiceAccountCredentials::Create(const Json& key_file_contents,
                                      std::string audience) {
  absl::StatusOr<Info> info = ParseServiceAccountJson(key_file_contents);
  if (!info.ok()) return info.status();

  RefCountedPtr<GDCHServiceAccountCredentials> creds =
      MakeRefCounted<GDCHServiceAccountCredentials>(*std::move(info),
                                                    std::move(audience));
  return creds;
}

GDCHServiceAccountCredentials::GDCHServiceAccountCredentials(
    Info info, std::string audience)
    : info_(std::move(info)), audience_(std::move(audience)) {}

GDCHServiceAccountCredentials::AssertionComponents
GDCHServiceAccountCredentials::AssertionComponentsFromInfo(
    const Info& info, std::chrono::system_clock::time_point now) {
  Json header = Json::FromObject({
      {"alg", Json::FromString("ES256")},
      {"typ", Json::FromString("JWT")},
      {"kid", Json::FromString(info.private_key_id)},
  });

  std::chrono::system_clock::time_point expiration = now + kTokenLifetime;
  const std::intmax_t now_from_epoch =
      static_cast<std::intmax_t>(std::chrono::system_clock::to_time_t(now));
  const std::intmax_t expiration_from_epoch = static_cast<std::intmax_t>(
      std::chrono::system_clock::to_time_t(expiration));
  std::string iss_sub_value =
      absl::StrCat("system:serviceaccount:", info.project_id, ":",
                   info.service_identity_name);

  Json claim = Json::FromObject({
      {"iss", Json::FromString(iss_sub_value)},
      {"sub", Json::FromString(iss_sub_value)},
      {"aud", Json::FromString(info.token_uri)},
      {"iat", Json::FromNumber(now_from_epoch)},
      {"exp", Json::FromNumber(expiration_from_epoch)},
  });

  return {JsonDump(header), JsonDump(claim)};
}

absl::StatusOr<std::string> GDCHServiceAccountCredentials::MakeJWTAssertion(
    const std::string& header, const std::string& claim,
    const std::string& pem_contents, SignatureFormat format) {
  const std::string body = absl::StrCat(absl::WebSafeBase64Escape(header), ".",
                                        absl::WebSafeBase64Escape(claim));
  absl::StatusOr<std::vector<std::uint8_t>> pem_signature =
      SignUsingSha256(body, pem_contents, format);
  if (!pem_signature.ok()) return std::move(pem_signature).status();
  std::string sig_str{pem_signature->begin(), pem_signature->end()};
  return absl::StrCat(body, ".", absl::WebSafeBase64Escape(sig_str));
}

absl::StatusOr<std::string> GDCHServiceAccountCredentials::CreateRequestBody(
    const Info& info, const std::string& audience) {
  AssertionComponents components =
      AssertionComponentsFromInfo(info, std::chrono::system_clock::now());
  absl::StatusOr<std::string> jwt =
      MakeJWTAssertion(components.header, components.claim, info.private_key,
                       SignatureFormat::kRaw);
  if (!jwt.ok()) return jwt.status();

  Json payload = Json::FromObject({
      {"grant_type",
       Json::FromString("urn:ietf:params:oauth:token-type:token-exchange")},
      {"audience", Json::FromString(audience)},
      {"requested_token_type",
       Json::FromString("urn:ietf:params:oauth:token-type:access_token")},
      {"subject_token", Json::FromString(std::move(*jwt))},
      {"subject_token_type",
       Json::FromString("urn:k8s:params:oauth:token-type:serviceaccount")},
  });

  return JsonDump(payload);
}

void GDCHServiceAccountCredentials::GrpcDeleter::operator()(
    grpc_http_request* ptr) {
  grpc_http_request_destroy(ptr);
  delete ptr;
}

absl::StatusOr<GDCHServiceAccountCredentials::GrpcHttpRequestUniquePtr>
GDCHServiceAccountCredentials::FormatHttpRequest(const Info& info,
                                                 const std::string& audience) {
  absl::StatusOr<std::string> body = CreateRequestBody(info, audience);
  if (!body.ok()) return body.status();

  GrpcHttpRequestUniquePtr request(new grpc_http_request);
  memset(request.get(), 0, sizeof(grpc_http_request));
  absl::StatusOr<URI> url = URI::Parse(info.token_uri);
  if (!url.ok()) return url.status();
  request->path = gpr_strdup(url->path().empty() ? "/" : url->path().c_str());
  request->hdr_count = 1;
  request->hdrs =
      static_cast<grpc_http_header*>(gpr_malloc(sizeof(grpc_http_header)));
  request->hdrs[0].key = gpr_strdup("content-type");
  request->hdrs[0].value = gpr_strdup("application/json");

  request->body_length = static_cast<int>(body->size());
  request->body = gpr_strdup(body->c_str());
  return request;
}

absl::StatusOr<std::string> GDCHServiceAccountCredentials::ParseHttpResponse(
    absl::string_view response_body) {
  absl::StatusOr<Json> response_json = JsonParse(response_body);
  if (!response_json.ok() || response_json->type() != Json::Type::kObject) {
    return absl::InternalError(
        "The format of response is not a valid json object.");
  }
  Json::Object::const_iterator response_it =
      response_json->object().find("access_token");
  if (response_it == response_json->object().end()) {
    return absl::InternalError("access_token field not present.");
  }
  if (response_it->second.type() != Json::Type::kString) {
    return absl::InternalError("access_token field must be a string.");
  }
  return response_it->second.string();
}

UniqueTypeName GDCHServiceAccountCredentials::type() const {
  static UniqueTypeName::Factory kFactory("GDCHServiceAccountCredentials");
  return kFactory.Create();
}

std::string GDCHServiceAccountCredentials::debug_string() {
  return absl::StrCat("GDCHServiceAccountCredentials{Audience:", audience_,
                      "}");
}

OrphanablePtr<HttpRequest> GDCHServiceAccountCredentials::StartHttpRequest(
    grpc_polling_entity* pollent, Timestamp deadline,
    grpc_http_response* response, grpc_closure* on_complete) {
  absl::StatusOr<URI> url = URI::Parse(info_.token_uri);
  if (!url.ok()) {
    ExecCtx::Run(DEBUG_LOCATION, on_complete, url.status());
    return nullptr;
  }
  absl::StatusOr<GrpcHttpRequestUniquePtr> request =
      FormatHttpRequest(info_, audience_);
  if (!request.ok()) {
    ExecCtx::Run(DEBUG_LOCATION, on_complete, request.status());
    return nullptr;
  }
  RefCountedPtr<grpc_channel_credentials> http_request_creds;
  if (url->scheme() == "http") {
    http_request_creds = RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  } else {
    http_request_creds = CreateHttpRequestSSLCredentials();
  }
  OrphanablePtr<HttpRequest> http_request = HttpRequest::Post(
      std::move(*url), /*args=*/nullptr, pollent, request->get(), deadline,
      on_complete, response, std::move(http_request_creds));
  http_request->Start();
  return http_request;
}

absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>>
GDCHServiceAccountCredentials::ExtractToken(
    const grpc_http_response& response) {
  absl::string_view response_body(response.body, response.body_length);
  absl::StatusOr<std::string> token_str = ParseHttpResponse(response_body);
  if (!token_str.ok()) return token_str.status();
  return MakeRefCounted<Token>(
      Slice::FromCopiedString(absl::StrCat("Bearer ", *token_str)),
      Timestamp::Now() + Duration::Seconds(3600));
}

}  // namespace grpc_core

grpc_call_credentials* grpc_gdch_service_account_credentials_create(
    const char* json_string, const char* audience_string) {
  absl::StatusOr<grpc_core::Json> json = grpc_core::JsonParse(json_string);
  if (!json.ok()) {
    LOG(ERROR) << "GDCH service account credentials creation failed. Error: "
               << json.status();
    return nullptr;
  }
  absl::StatusOr<
      grpc_core::RefCountedPtr<grpc_core::GDCHServiceAccountCredentials>>
      creds = grpc_core::GDCHServiceAccountCredentials::Create(*json,
                                                               audience_string);
  if (!creds.ok()) {
    LOG(ERROR) << "GDCH service account credentials creation failed. Error: "
               << grpc_core::StatusToString(creds.status());
    return nullptr;
  }
  return creds->release();
}
