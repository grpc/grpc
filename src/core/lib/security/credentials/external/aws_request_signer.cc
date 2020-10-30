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
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>

static const std::string kAlgorithm = "AWS4-HMAC-SHA256";
static const std::string kDateFormat = "%a, %d %b %E4Y %H:%M:%S %Z";
static const std::string kXAmzDateFormat = "%Y%m%dT%H%M%SZ";

namespace grpc_core {

void SHA256(const std::string str, unsigned char out[SHA256_DIGEST_LENGTH]) {
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, str.c_str(), strlen(str.c_str()));
  SHA256_Final(out, &sha256);
}

const std::string SHA256Hex(const std::string str) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(str, hash);
  std::string hash_str(reinterpret_cast<char const*>(hash),
                       SHA256_DIGEST_LENGTH);
  return absl::BytesToHexString(hash_str);
}

const std::string HMAC(const std::string key, const std::string msg) {
  unsigned int len;
  unsigned char digest[EVP_MAX_MD_SIZE];
  HMAC(EVP_sha256(), key.c_str(), key.length(),
       (const unsigned char*)msg.c_str(), msg.length(), digest, &len);
  return std::string(digest, digest + len);
}

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
  if (additional_headers_.find("x-amz-date") != additional_headers_.end() &&
      additional_headers_.find("date") != additional_headers_.end()) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Only one of {date, x-amz-date} can be specified, not both.");
    return std::map<std::string, std::string>();
  } else if (additional_headers_.find("x-amz-date") !=
             additional_headers_.end()) {
    request_date_full = additional_headers_["x-amz-date"];
    request_date_short = request_date_full.substr(0, 8);
  } else if (additional_headers_.find("date") != additional_headers_.end()) {
    absl::Time request_date;
    std::string err_str;
    if (!absl::ParseTime(kDateFormat, additional_headers_["date"],
                         &request_date, &err_str)) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(err_str.c_str());
      return std::map<std::string, std::string>();
    }
    request_date_full =
        FormatTime(kXAmzDateFormat, request_date, absl::UTCTimeZone());
    request_date_short = request_date_full.substr(0, 8);
  } else {
    absl::Time request_date = absl::Now();
    request_date_full =
        FormatTime(kXAmzDateFormat, request_date, absl::UTCTimeZone());
    request_date_short = request_date_full.substr(0, 8);
  }
  // TASK 1: Create a canonical request for Signature Version 4
  // https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
  std::string canonical_request;
  // 1. HTTPRequestMethod
  canonical_request += method_;
  canonical_request += "\n";
  // 2. CanonicalURI
  grpc_uri* uri = grpc_uri_parse(url_, false);
  std::string canonical_uri(uri->path);
  if (canonical_uri.empty()) {
    canonical_uri = "/";
  }
  canonical_request += canonical_uri;
  canonical_request += "\n";
  // 3. CanonicalQueryString
  std::string canonical_query_str(uri->query);
  canonical_request += canonical_query_str;
  canonical_request += "\n";
  // 4. CanonicalHeaders
  auto headers = std::map<std::string, std::string>();
  headers.insert({"host", uri->authority});
  if (additional_headers_.find("date") == additional_headers_.end()) {
    headers.insert({"x-amz-date", request_date_full});
  }
  if (!token_.empty()) {
    headers.insert({"x-amz-security-token", token_});
  }
  for (auto header : additional_headers_) {
    headers.insert({absl::AsciiStrToLower(header.first), header.second});
  }
  std::string canonical_headers;
  for (auto header : headers) {
    canonical_headers += header.first;
    canonical_headers += ":";
    canonical_headers += header.second;
    canonical_headers += "\n";
  }
  canonical_request += canonical_headers;
  canonical_request += "\n";
  // 5. SignedHeaders
  std::string signed_headers;
  for (auto header : headers) {
    signed_headers += header.first;
    signed_headers += ";";
  }
  signed_headers.pop_back();
  canonical_request += signed_headers;
  canonical_request += "\n";
  // 6. RequestPayload
  canonical_request += SHA256Hex(request_payload_);
  std::cout << "1 ------ \n" << canonical_request << std::endl;
  // TASK 2: Create a string to sign for Signature Version 4
  // https://docs.aws.amazon.com/general/latest/gr/sigv4-create-string-to-sign.html
  std::string string_to_sign;
  // 1. Algorithm
  string_to_sign += kAlgorithm;
  string_to_sign += "\n";
  // 2. RequestDateTime
  string_to_sign += request_date_full;
  string_to_sign += "\n";
  // 3. CredentialScope
  std::string host(uri->authority);
  std::string service_name = host.substr(0, host.find("."));
  std::string credential_scope = absl::StrFormat(
      "%s/%s/%s/aws4_request", request_date_short, region_, service_name);
  string_to_sign += credential_scope;
  string_to_sign += "\n";
  grpc_uri_destroy(uri);
  // 4. HashedCanonicalRequest
  string_to_sign += SHA256Hex(canonical_request);
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
  headers.insert({"Authorization", authorization_header});
  return headers;
}

}  // namespace grpc_core
