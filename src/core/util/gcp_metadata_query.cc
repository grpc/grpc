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

#include "src/core/util/gcp_metadata_query.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/port_platform.h>
#include <string.h>

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/time.h"
#include "src/core/util/uri.h"

namespace grpc_core {

constexpr const char GcpMetadataQuery::kZoneAttribute[];
constexpr const char GcpMetadataQuery::kClusterNameAttribute[];
constexpr const char GcpMetadataQuery::kRegionAttribute[];
constexpr const char GcpMetadataQuery::kInstanceIdAttribute[];
constexpr const char GcpMetadataQuery::kIPv6Attribute[];

GcpMetadataQuery::GcpMetadataQuery(
    std::string attribute, grpc_polling_entity* pollent,
    absl::AnyInvocable<void(std::string /* attribute */,
                            absl::StatusOr<std::string> /* result */)>
        callback,
    Duration timeout)
    : GcpMetadataQuery("metadata.google.internal.", std::move(attribute),
                       pollent, std::move(callback), timeout) {}

GcpMetadataQuery::GcpMetadataQuery(
    std::string metadata_server_name, std::string attribute,
    grpc_polling_entity* pollent,
    absl::AnyInvocable<void(std::string /* attribute */,
                            absl::StatusOr<std::string> /* result */)>
        callback,
    Duration timeout)
    : InternallyRefCounted<GcpMetadataQuery>(nullptr, 2),
      attribute_(std::move(attribute)),
      callback_(std::move(callback)) {
  GRPC_CLOSURE_INIT(&on_done_, OnDone, this, nullptr);
  auto uri = URI::Create("http", std::move(metadata_server_name), attribute_,
                         {} /* query params */, "" /* fragment */);
  CHECK(uri.ok());  // params are hardcoded
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

GcpMetadataQuery::~GcpMetadataQuery() {
  grpc_http_response_destroy(&response_);
}

void GcpMetadataQuery::Orphan() {
  http_request_.reset();
  Unref();
}

void GcpMetadataQuery::OnDone(void* arg, grpc_error_handle error) {
  auto* self = static_cast<GcpMetadataQuery*>(arg);
  GRPC_TRACE_LOG(metadata_query, INFO)
      << "MetadataServer Query for " << self->attribute_
      << ": HTTP status: " << self->response_.status
      << ", error: " << StatusToString(error);
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
      GRPC_TRACE_LOG(metadata_query, INFO) << result.status();
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
