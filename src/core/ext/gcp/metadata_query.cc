//
//
// Copyright 2023 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/gcp/metadata_query.h"

#include <string.h>

#include <initializer_list>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

TraceFlag grpc_metadata_query_trace(false, "metadata_query");

constexpr const char MetadataQuery::kZoneAttribute[] =
    "/computeMetadata/v1/instance/zone";
constexpr const char MetadataQuery::kClusterNameAttribute[] =
    "/computeMetadata/v1/instance/attributes/cluster-name";
constexpr const char MetadataQuery::kRegionAttribute[] =
    "/computeMetadata/v1/instance/region";
constexpr const char MetadataQuery::kInstanceIdAttribute[] =
    "/computeMetadata/v1/instance/id";
constexpr const char MetadataQuery::kIPv6Attribute[] =
    "/computeMetadata/v1/instance/network-interfaces/0/ipv6s";

MetadataQuery::MetadataQuery(
    std::string attribute, grpc_polling_entity* pollent,
    absl::AnyInvocable<void(std::string /* attribute */,
                            absl::StatusOr<std::string> /* result */)>
        callback,
    Duration timeout)
    : MetadataQuery("metadata.google.internal.", std::move(attribute), pollent,
                    std::move(callback), timeout) {}

MetadataQuery::MetadataQuery(
    std::string metadata_server_name, std::string attribute,
    grpc_polling_entity* pollent,
    absl::AnyInvocable<void(std::string /* attribute */,
                            absl::StatusOr<std::string> /* result */)>
        callback,
    Duration timeout)
    : InternallyRefCounted<MetadataQuery>(nullptr, 2),
      attribute_(std::move(attribute)),
      callback_(std::move(callback)) {
  GRPC_CLOSURE_INIT(&on_done_, OnDone, this, nullptr);
  auto uri = URI::Create("http", std::move(metadata_server_name), attribute_,
                         {} /* query params */, "" /* fragment */);
  GPR_ASSERT(uri.ok());  // params are hardcoded
  grpc_http_request request;
  memset(&request, 0, sizeof(grpc_http_request));
  grpc_http_header header = {const_cast<char*>("Metadata-Flavor"),
                             const_cast<char*>("Google")};
  request.hdr_count = 1;
  request.hdrs = &header;
  http_request_ = HttpRequest::Get(
      std::move(*uri), nullptr /* channel args */, pollent, &request,
      Timestamp::Now() + timeout, &on_done_, &response_,
      RefCountedPtr<grpc_channel_credentials>(
          grpc_insecure_credentials_create()));
  http_request_->Start();
}

MetadataQuery::~MetadataQuery() { grpc_http_response_destroy(&response_); }

void MetadataQuery::Orphan() {
  http_request_.reset();
  Unref();
}

void MetadataQuery::OnDone(void* arg, grpc_error_handle error) {
  auto* self = static_cast<MetadataQuery*>(arg);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_metadata_query_trace)) {
    gpr_log(GPR_INFO, "MetadataServer Query for %s: HTTP status: %d, error: %s",
            self->attribute_.c_str(), self->response_.status,
            StatusToString(error).c_str());
  }
  absl::StatusOr<std::string> result;
  if (!error.ok()) {
    result = absl::UnavailableError(absl::StrFormat(
        "MetadataServer Query failed for %s: %s", self->attribute_.c_str(),
        StatusToString(error).c_str()));
  } else if (self->response_.status != 200) {
    result = absl::UnavailableError(absl::StrFormat(
        "MetadataServer Query received non-200 status for %s: %s",
        self->attribute_.c_str(), StatusToString(error).c_str()));
  } else if (self->attribute_ == kZoneAttribute) {
    absl::string_view body(self->response_.body, self->response_.body_length);
    size_t pos = body.find_last_of('/');
    if (pos == body.npos) {
      result = absl::UnavailableError(
          absl::StrFormat("MetadataServer Could not parse zone: %s",
                          std::string(body).c_str()));
      if (GRPC_TRACE_FLAG_ENABLED(grpc_metadata_query_trace)) {
        gpr_log(GPR_INFO, "%s", result.status().ToString().c_str());
      }
    } else {
      result = std::string(body.substr(pos + 1));
    }
  } else {
    result = std::string(self->response_.body, self->response_.body_length);
  }
  auto callback = std::move(self->callback_);
  auto attribute = std::move(self->attribute_);
  self->Unref();
  return callback(std::move(attribute), std::move(result));
}

}  // namespace grpc_core
