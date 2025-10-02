//
//
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
//
//

#include "src/core/credentials/transport/tls/spiffe_utils.h"

#include <openssl/x509.h>

#include <string>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "src/core/tsi/ssl_transport_security_utils.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/load_file.h"
#include "src/core/util/status_helper.h"

namespace grpc_core {
namespace {
constexpr absl::string_view kAllowedUse = "x509-svid";
const std::set<absl::string_view> kAllowedKtys = {"RSA", "EC"};
constexpr absl::string_view kCertificatePrefix =
    "-----BEGIN CERTIFICATE-----\n";
constexpr absl::string_view kCertificateSuffix = "\n-----END CERTIFICATE-----";
constexpr int kMaxTrustDomainLength = 255;
constexpr absl::string_view kSpiffePrefix = "spiffe://";
constexpr int kX5cSize = 1;

// Checks broad conditions on the whole input before splitting into the
// pieces of a SPIFFE ID
absl::Status DoInitialUriValidation(absl::string_view uri) {
  if (uri.empty()) {
    return absl::InvalidArgumentError(
        "SPIFFE ID cannot be parsed from empty URI");
  }
  if (uri.length() > 2048) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "URI length is %d, maximum allowed for SPIFFE ID is 2048",
        uri.length()));
  }
  if (absl::StrContains(uri, "#")) {
    return absl::InvalidArgumentError(
        "SPIFFE ID cannot contain query fragments");
  }
  if (absl::StrContains(uri, "?")) {
    return absl::InvalidArgumentError(
        "SPIFFE ID cannot contain query parameters");
  }
  for (char ch : uri) {
    if (!absl::ascii_isascii(ch)) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "SPIFFE ID URI cannot contain non-ascii characters. Contains %#x",
          ch));
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateTrustDomain(absl::string_view trust_domain) {
  if (trust_domain.empty()) {
    return absl::InvalidArgumentError("Trust domain cannot be empty");
  }
  if (trust_domain.size() > kMaxTrustDomainLength) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Trust domain maximum length is %i characters", kMaxTrustDomainLength));
  }
  for (auto c : trust_domain) {
    if (c >= 'a' && c <= 'z') continue;
    if (c >= '0' && c <= '9') continue;
    if (c == '.') continue;
    if (c == '-') continue;
    if (c == '_') continue;
    return absl::InvalidArgumentError(absl::StrFormat(
        "Trust domain contains invalid character '%c'. MUST contain only "
        "lowercase letters, numbers, dots, dashes, and underscores",
        c));
  }
  return absl::OkStatus();
}

absl::Status ValidatePathSegment(absl::string_view path_segment) {
  if (path_segment.empty()) {
    return absl::InvalidArgumentError("Path segment cannot be empty");
  }
  if (path_segment == "." || path_segment == "..") {
    return absl::InvalidArgumentError(
        "Path segment cannot be a relative modifier (. or ..)");
  }
  for (auto c : path_segment) {
    if (c >= 'a' && c <= 'z') continue;
    if (c >= 'A' && c <= 'Z') continue;
    if (c >= '0' && c <= '9') continue;
    if (c == '.') continue;
    if (c == '-') continue;
    if (c == '_') continue;
    return absl::InvalidArgumentError(absl::StrFormat(
        "Path segment contains invalid character '%c'. MUST contain only "
        "letters, numbers, dots, dashes, and underscores",
        c));
  }
  return absl::OkStatus();
}

absl::Status ValidatePath(absl::string_view path) {
  if (path.empty()) {
    return absl::OkStatus();
  }
  for (absl::string_view segment : absl::StrSplit(path, '/')) {
    GRPC_RETURN_IF_ERROR(ValidatePathSegment(segment));
  }
  return absl::OkStatus();
}

}  // namespace

std::string AddPemBlockWrapping(absl::string_view spiffe_bundle_root) {
  return absl::StrCat(kCertificatePrefix, spiffe_bundle_root,
                      kCertificateSuffix);
}

absl::StatusOr<SpiffeId> SpiffeId::FromString(absl::string_view input) {
  GRPC_RETURN_IF_ERROR(DoInitialUriValidation(input));
  if (!absl::StartsWithIgnoreCase(input, kSpiffePrefix)) {
    return absl::InvalidArgumentError("SPIFFE ID must start with spiffe://");
  }
  if (absl::EndsWith(input, /*suffix=*/"/")) {
    return absl::InvalidArgumentError("SPIFFE ID cannot end with a /");
  }
  // The input definitely starts with spiffe://
  absl::string_view trust_domain_and_path =
      input.substr(kSpiffePrefix.length());
  absl::string_view trust_domain;
  absl::string_view path;
  if (absl::StartsWith(trust_domain_and_path, "/")) {
    // To be here the SPIFFE ID must look like spiffe:///path, which means the
    // trust domain is empty, which is invalid
    return absl::InvalidArgumentError("The trust domain cannot be empty");
  }
  // It's valid to have no path, e.g. spiffe://foo.bar.com - handle those two
  // cases
  if (absl::StrContains(trust_domain_and_path, "/")) {
    std::vector<absl::string_view> split =
        absl::StrSplit(trust_domain_and_path, absl::MaxSplits('/', 1));
    trust_domain = split[0];
    path = split[1];
  } else {
    trust_domain = trust_domain_and_path;
  }
  GRPC_RETURN_IF_ERROR(ValidateTrustDomain(trust_domain));
  GRPC_RETURN_IF_ERROR(ValidatePath(path));
  // If we have a path re-add the prepending `/`, otherwise leave it empty
  if (path.empty()) {
    return SpiffeId(trust_domain, "");
  }
  return SpiffeId(trust_domain, absl::StrCat("/", path));
}

const JsonLoaderInterface* SpiffeBundleKey::JsonLoader(const JsonArgs&) {
  static const auto* kLoader = JsonObjectLoader<SpiffeBundleKey>().Finish();
  return kLoader;
}

void SpiffeBundleKey::JsonPostLoad(const Json& json, const JsonArgs& args,
                                   ValidationErrors* errors) {
  auto use =
      LoadJsonObjectField<std::string>(json.object(), args, "use", errors);
  {
    ValidationErrors::ScopedField field(errors, ".use");
    if (use.has_value() && *use != kAllowedUse) {
      errors->AddError(absl::StrFormat("value must be \"%s\", got \"%s\"",
                                       kAllowedUse, *use));
    }
  }
  auto kty =
      LoadJsonObjectField<std::string>(json.object(), args, "kty", errors);
  {
    ValidationErrors::ScopedField field(errors, ".kty");
    if (kty.has_value()) {
      if (kAllowedKtys.find(*kty) == kAllowedKtys.end()) {
        errors->AddError(
            absl::StrFormat("value must be one of \"%s\", got \"%s\"",
                            absl::StrJoin(kAllowedKtys, "\", \""), *kty));
      }
    }
  }
  auto x5c = LoadJsonObjectField<std::vector<std::string>>(json.object(), args,
                                                           "x5c", errors);
  if (x5c.has_value()) {
    ValidationErrors::ScopedField field(errors, ".x5c");
    if (x5c->size() != kX5cSize) {
      errors->AddError(
          absl::StrCat("array length must be 1, got ", x5c->size()));
    }
    if (!x5c->empty()) {
      ValidationErrors::ScopedField field(errors, "[0]");
      std::string pem_cert = AddPemBlockWrapping((*x5c)[0]);
      auto certs = ParsePemCertificateChain(pem_cert);
      if (!certs.ok()) {
        errors->AddError(certs.status().ToString());
      } else {
        root_ = std::move((*x5c)[0]);
        for (X509* cert : *certs) {
          X509_free(cert);
        }
      }
    }
  }
}

absl::string_view SpiffeBundleKey::GetRoot() { return root_; }

const JsonLoaderInterface* SpiffeBundle::JsonLoader(const JsonArgs&) {
  static const auto* kLoader = JsonObjectLoader<SpiffeBundle>().Finish();
  return kLoader;
}

void SpiffeBundle::JsonPostLoad(const Json& json, const JsonArgs& args,
                                ValidationErrors* errors) {
  auto keys = LoadJsonObjectField<std::vector<SpiffeBundleKey>>(
      json.object(), args, "keys", errors);
  if (!keys.has_value()) {
    return;
  }
  for (size_t i = 0; i < keys->size(); ++i) {
    roots_.emplace_back((*keys)[i].GetRoot());
  }
  ValidationErrors::ScopedField field(errors, "keys");
  absl::Status status = CreateX509Stack();
  if (!status.ok()) {
    errors->AddError(status.ToString());
  }
}

SpiffeBundle::~SpiffeBundle() {
  if (root_stack_ != nullptr) {
    sk_X509_pop_free(*root_stack_, X509_free);
  }
}

SpiffeBundle::SpiffeBundle(const SpiffeBundle& other) {
  roots_ = other.roots_;
  if (other.root_stack_ != nullptr) {
    root_stack_ =
        std::make_unique<STACK_OF(X509)*>(sk_X509_dup(*other.root_stack_));
    for (size_t i = 0; i < sk_X509_num(*root_stack_); i++) {
      X509* x = sk_X509_value(*root_stack_, i);
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      CHECK(X509_up_ref(x));
#else
      CRYPTO_add(&x->references, 1, CRYPTO_LOCK_X509);
#endif
    }
  }
}

SpiffeBundle& SpiffeBundle::operator=(const SpiffeBundle& other) {
  if (this != &other) {
    roots_ = other.roots_;
    if (other.root_stack_ != nullptr) {
      root_stack_ =
          std::make_unique<STACK_OF(X509)*>(sk_X509_dup(*other.root_stack_));
      for (size_t i = 0; i < sk_X509_num(*root_stack_); i++) {
        X509* x = sk_X509_value(*root_stack_, i);
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
        CHECK(X509_up_ref(x));
#else
        CRYPTO_add(&x->references, 1, CRYPTO_LOCK_X509);
#endif
      }
    }
  }
  return *this;
}

const JsonLoaderInterface* SpiffeBundleMap::JsonLoader(const JsonArgs&) {
  static const auto* kLoader =
      JsonObjectLoader<SpiffeBundleMap>()
          .Field("trust_domains", &SpiffeBundleMap::bundles_)
          .Finish();
  return kLoader;
}

absl::Span<const std::string> SpiffeBundle::GetRoots() { return roots_; }

absl::StatusOr<STACK_OF(X509) *> SpiffeBundle::GetRootStack() {
  if (root_stack_ == nullptr) {
    return absl::FailedPreconditionError(
        "root_stack_ has not been initialized");
  }
  return *root_stack_;
}

absl::Status SpiffeBundle::CreateX509Stack() {
  root_stack_ = std::make_unique<STACK_OF(X509)*>(sk_X509_new_null());
  absl::Status status = absl::OkStatus();
  for (const auto& pem_cert : roots_) {
    auto cert = ParsePemCertificateChain(AddPemBlockWrapping(pem_cert));
    if (!cert.status().ok()) {
      status = cert.status();
      break;
    }
    if (cert->size() != 1) {
      status = absl::InvalidArgumentError("Got a malformed root certificate.");
      break;
    }
    sk_X509_push(*root_stack_, (*cert)[0]);
  }
  // If there was an error parsing we don't want a partially filled root stack.
  if (!status.ok()) {
    sk_X509_pop_free(*root_stack_, X509_free);
  }
  return status;
}

void SpiffeBundleMap::JsonPostLoad(const Json&, const JsonArgs&,
                                   ValidationErrors* errors) {
  {
    for (auto const& [k, _] : bundles_) {
      ValidationErrors::ScopedField field(
          errors, absl::StrCat(".trust_domains[\"", k, "\"]"));
      absl::Status status = ValidateTrustDomain(k);
      if (!status.ok()) {
        errors->AddError(
            absl::StrCat("invalid trust domain: ", status.ToString()));
      }
    }
  }
}

absl::StatusOr<SpiffeBundleMap> SpiffeBundleMap::FromFile(
    absl::string_view file_path) {
  auto slice = LoadFile(file_path.data(), /*add_null_terminator=*/false);
  GRPC_RETURN_IF_ERROR(slice.status());
  auto json = JsonParse(slice->as_string_view());
  GRPC_RETURN_IF_ERROR(json.status());
  return LoadFromJson<SpiffeBundleMap>(*json);
}

absl::StatusOr<absl::Span<const std::string>> SpiffeBundleMap::GetRoots(
    const absl::string_view trust_domain) {
  if (auto it = bundles_.find(trust_domain); it != bundles_.end()) {
    return it->second.GetRoots();
  }
  return absl::NotFoundError(absl::StrFormat(
      "No spiffe bundle found for trust domain %s", trust_domain));
}

absl::StatusOr<STACK_OF(X509) *> SpiffeBundleMap::GetRootStack(
    absl::string_view trust_domain) {
  if (auto it = bundles_.find(trust_domain); it != bundles_.end()) {
    return it->second.GetRootStack();
  }
  return absl::NotFoundError(absl::StrFormat(
      "No spiffe bundle found for trust domain %s", trust_domain));
}

}  // namespace grpc_core
