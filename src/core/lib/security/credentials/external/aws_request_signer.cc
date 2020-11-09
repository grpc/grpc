//
// Copyright 2020 gRPC authors.
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
#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/external/aws_request_signer.h"

#include <iostream>

#include "src/core/lib/uri/uri_parser.h"

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace grpc_core {

namespace {

const char kAlgorithm[] = "AWS4-HMAC-SHA256";
const char kDateFormat[] = "%a, %d %b %E4Y %H:%M:%S %Z";
const char kXAmzDateFormat[] = "%Y%m%dT%H%M%SZ";

void SHA256(const std::string& str, unsigned char out[SHA256_DIGEST_LENGTH]) {
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, str.c_str(), str.size());
  SHA256_Final(out, &sha256);
}

std::string SHA256Hex(const std::string& str) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(str, hash);
  std::string hash_str(reinterpret_cast<char const*>(hash),
                       SHA256_DIGEST_LENGTH);
  return absl::BytesToHexString(hash_str);
}

std::string HMAC(const std::string& key, const std::string& msg) {
  unsigned int len;
  unsigned char digest[EVP_MAX_MD_SIZE];
  HMAC(EVP_sha256(), key.c_str(), key.length(),
       (const unsigned char*)msg.c_str(), msg.length(), digest, &len);
  return std::string(digest, digest + len);
}

}  // namespace

AwsRequestSigner::AwsRequestSigner(
    std::string access_key_id, std::string secret_access_key, std::string token,
    std::string method, std::string url, std::string region,
    std::string request_payload,
    std::map<std::string, std::string> additional_headers)
    : access_key_id_(std::move(access_key_id)),
      secret_access_key_(std::move(secret_access_key)),
      token_(std::move(token)),
      method_(std::move(method)),
      url_(std::move(url)),
      region_(std::move(region)),
      request_payload_(std::move(request_payload)),
      additional_headers_(std::move(additional_headers)) {}

std::map<std::string, std::string> AwsRequestSigner::GetSignedRequestHeaders(
    grpc_error** error) {
  std::string request_date_full;
  std::string request_date_short;
  auto amz_date_it = additional_headers_.find("x-amz-date");
  auto date_it = additional_headers_.find("date");
  if (amz_date_it != additional_headers_.end() &&
      date_it != additional_headers_.end()) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Only one of {date, x-amz-date} can be specified, not both.");
    return {};
  }
  if (amz_date_it != additional_headers_.end()) {
    request_date_full = amz_date_it->second;
    request_date_short = request_date_full.substr(0, 8);
  } else if (date_it != additional_headers_.end()) {
    absl::Time request_date;
    std::string err_str;
    if (!absl::ParseTime(kDateFormat, date_it->second, &request_date,
                         &err_str)) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(err_str.c_str());
      return {};
    }
    request_date_full =
        absl::FormatTime(kXAmzDateFormat, request_date, absl::UTCTimeZone());
    request_date_short = request_date_full.substr(0, 8);
  } else {
    absl::Time request_date = absl::Now();
    request_date_full =
        absl::FormatTime(kXAmzDateFormat, request_date, absl::UTCTimeZone());
    request_date_short = request_date_full.substr(0, 8);
  }
  // TASK 1: Create a canonical request for Signature Version 4
  // https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
  std::vector<absl::string_view> canonical_request_vector;
  // 1. HTTPRequestMethod
  canonical_request_vector.emplace_back(method_);
  canonical_request_vector.emplace_back("\n");
  // 2. CanonicalURI
  grpc_uri* uri = grpc_uri_parse(url_, false);
  if (uri == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Invalid Aws request url.");
    return {};
  }
  std::string canonical_uri(uri->path);
  if (canonical_uri.empty()) {
    canonical_uri = "/";
  }
  canonical_request_vector.emplace_back(canonical_uri);
  canonical_request_vector.emplace_back("\n");
  // 3. CanonicalQueryString
  std::string canonical_query_str(uri->query);
  canonical_request_vector.emplace_back(canonical_query_str);
  canonical_request_vector.emplace_back("\n");
  // 4. CanonicalHeaders
  if (request_headers_.empty()) {
    request_headers_.insert({"host", uri->authority});
    if (!token_.empty()) {
      request_headers_.insert({"x-amz-security-token", token_});
    }
    for (const auto& header : additional_headers_) {
      request_headers_.insert(
          {absl::AsciiStrToLower(header.first), header.second});
    }
  }
  if (additional_headers_.find("date") == additional_headers_.end()) {
    request_headers_["x-amz-date"] = request_date_full;
  }
  std::vector<absl::string_view> canonical_headers_vector;
  for (const auto& header : request_headers_) {
    canonical_headers_vector.emplace_back(header.first);
    canonical_headers_vector.emplace_back(":");
    canonical_headers_vector.emplace_back(header.second);
    canonical_headers_vector.emplace_back("\n");
  }
  std::string canonical_headers = absl::StrJoin(canonical_headers_vector, "");
  canonical_request_vector.emplace_back(canonical_headers);
  canonical_request_vector.emplace_back("\n");
  // 5. SignedHeaders
  std::vector<absl::string_view> signed_headers_vector;
  for (const auto& header : request_headers_) {
    signed_headers_vector.emplace_back(header.first);
  }
  std::string signed_headers = absl::StrJoin(signed_headers_vector, ";");
  canonical_request_vector.emplace_back(signed_headers);
  canonical_request_vector.emplace_back("\n");
  // 6. RequestPayload
  std::string hashed_request_payload = SHA256Hex(request_payload_);
  canonical_request_vector.emplace_back(hashed_request_payload);
  std::string canonical_request = absl::StrJoin(canonical_request_vector, "");
  std::cout << "1 ------ \n" << canonical_request << std::endl;
  // TASK 2: Create a string to sign for Signature Version 4
  // https://docs.aws.amazon.com/general/latest/gr/sigv4-create-string-to-sign.html
  std::vector<absl::string_view> string_to_sign_vector;
  // 1. Algorithm
  string_to_sign_vector.emplace_back("AWS4-HMAC-SHA256");
  string_to_sign_vector.emplace_back("\n");
  // 2. RequestDateTime
  string_to_sign_vector.emplace_back(request_date_full);
  string_to_sign_vector.emplace_back("\n");
  // 3. CredentialScope
  std::pair<absl::string_view, absl::string_view> host_parts =
      absl::StrSplit(uri->authority, absl::MaxSplits('.', 1));
  std::string service_name(host_parts.first);
  std::string credential_scope = absl::StrFormat(
      "%s/%s/%s/aws4_request", request_date_short, region_, service_name);
  string_to_sign_vector.emplace_back(credential_scope);
  string_to_sign_vector.emplace_back("\n");
  grpc_uri_destroy(uri);
  // 4. HashedCanonicalRequest
  std::string hashed_canonical_request = SHA256Hex(canonical_request);
  string_to_sign_vector.emplace_back(hashed_canonical_request);
  std::string string_to_sign = absl::StrJoin(string_to_sign_vector, "");
  std::cout << "2 ------\n" << string_to_sign << std::endl;
  // TASK 3: Task 3: Calculate the signature for AWS Signature Version 4
  // https://docs.aws.amazon.com/general/latest/gr/sigv4-calculate-signature.html
  // 1. Derive your signing key.
  std::string date = HMAC("AWS4" + secret_access_key_, request_date_short);
  std::string region = HMAC(date, region_);
  std::string service = HMAC(region, service_name);
  std::string signing = HMAC(service, "aws4_request");
  // 2. Calculate the signature.
  std::string signature_str = HMAC(signing, string_to_sign);
  std::string signature = absl::BytesToHexString(signature_str);
  std::cout << "3 ------\n" << std::endl << signature << std::endl;
  // TASK 4: Add the signature to the HTTP request
  // https://docs.aws.amazon.com/general/latest/gr/sigv4-add-signature-to-request.html
  std::string authorization_header = absl::StrFormat(
      "%s Credential=%s/%s, SignedHeaders=%s, Signature=%s", kAlgorithm,
      access_key_id_, credential_scope, signed_headers, signature);
  std::cout << "4 ------\n" << authorization_header << std::endl;
  request_headers_["Authorization"] = authorization_header;
  return request_headers_;
}

}  // namespace grpc_core
