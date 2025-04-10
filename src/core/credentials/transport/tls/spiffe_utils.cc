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

#include <string>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "src/core/util/status_helper.h"

namespace grpc_core {
namespace {

constexpr absl::string_view kSpiffePrefix = "spiffe://";
constexpr int kMaxTrustDomainLength = 255;

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
      return absl::InvalidArgumentError(
          absl::StrFormat("SPIFFE ID URI cannot contain non-ascii characters"));
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
        "Trust domain contains invalid character %c. MUST contain only "
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
        "Path segment contains invalid character %c. MUST contain only "
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

}  // namespace grpc_core