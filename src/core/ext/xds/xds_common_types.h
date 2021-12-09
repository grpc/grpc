//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_CORE_EXT_XDS_XDS_COMMON_TYPES_H
#define GRPC_CORE_EXT_XDS_XDS_COMMON_TYPES_H

#include <grpc/support/port_platform.h>

#include <string>
#include <vector>

#include "absl/strings/str_format.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"

#include "src/core/ext/xds/upb_utils.h"
#include "src/core/lib/matchers/matchers.h"

namespace grpc_core {

struct Duration {
  int64_t seconds = 0;
  int32_t nanos = 0;

  Duration() = default;

  bool operator==(const Duration& other) const {
    return seconds == other.seconds && nanos == other.nanos;
  }
  std::string ToString() const {
    return absl::StrFormat("Duration seconds: %ld, nanos %d", seconds, nanos);
  }

  static Duration Parse(const google_protobuf_Duration* proto_duration) {
    Duration duration;
    duration.seconds = google_protobuf_Duration_seconds(proto_duration);
    duration.nanos = google_protobuf_Duration_nanos(proto_duration);
    return duration;
  }
};

struct CommonTlsContext {
  struct CertificateProviderPluginInstance {
    std::string instance_name;
    std::string certificate_name;

    bool operator==(const CertificateProviderPluginInstance& other) const {
      return instance_name == other.instance_name &&
             certificate_name == other.certificate_name;
    }

    std::string ToString() const;
    bool Empty() const;
  };

  struct CertificateValidationContext {
    CertificateProviderPluginInstance ca_certificate_provider_instance;
    std::vector<StringMatcher> match_subject_alt_names;

    bool operator==(const CertificateValidationContext& other) const {
      return ca_certificate_provider_instance ==
                 other.ca_certificate_provider_instance &&
             match_subject_alt_names == other.match_subject_alt_names;
    }

    std::string ToString() const;
    bool Empty() const;
  };

  CertificateValidationContext certificate_validation_context;
  CertificateProviderPluginInstance tls_certificate_provider_instance;

  bool operator==(const CommonTlsContext& other) const {
    return certificate_validation_context ==
               other.certificate_validation_context &&
           tls_certificate_provider_instance ==
               other.tls_certificate_provider_instance;
  }

  std::string ToString() const;
  bool Empty() const;

  static grpc_error_handle Parse(
      const XdsEncodingContext& context,
      const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext*
          common_tls_context_proto,
      CommonTlsContext* common_tls_context);
};

grpc_error_handle ExtractHttpFilterTypeName(const XdsEncodingContext& context,
                                            const google_protobuf_Any* any,
                                            absl::string_view* filter_type);

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_COMMON_TYPES_H
