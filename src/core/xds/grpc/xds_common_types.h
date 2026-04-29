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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_COMMON_TYPES_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_COMMON_TYPES_H

#include <string>
#include <variant>
#include <vector>

#include "src/core/util/json/json.h"
#include "src/core/util/matchers.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

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
    struct SystemRootCerts {
      bool operator==(const SystemRootCerts&) const { return true; }
    };
    std::variant<std::monostate, CertificateProviderPluginInstance,
                 SystemRootCerts>
        ca_certs;
    std::vector<StringMatcher> match_subject_alt_names;

    bool operator==(const CertificateValidationContext& other) const {
      return ca_certs == other.ca_certs &&
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
};

struct XdsExtension {
  // The type, either from the top level or from inside the TypedStruct.
  absl::string_view type;
  // A Json object for a TypedStruct, or the serialized config otherwise.
  std::variant<absl::string_view /*serialized_value*/, Json /*typed_struct*/>
      value;
  // Validation fields that need to stay in scope until we're done
  // processing the extension.
  std::vector<ValidationErrors::ScopedField> validation_fields;
};

struct XdsGrpcService {
  std::unique_ptr<GrpcXdsServerTarget> server_target;
  Duration timeout;
  std::vector<std::pair<std::string, std::string>> initial_metadata;
};

struct HeaderMutationRules {
  bool disallow_all = false;
  bool disallow_is_error = false;
  std::unique_ptr<RE2> allow_expression;
  std::unique_ptr<RE2> disallow_expression;

  bool IsMutationAllowed(const std::string& header_name) const;

  std::string ToString() const;

  bool operator==(const HeaderMutationRules& other) const {
    auto is_re_equal = [](RE2* a, RE2* b) {
      if (a == nullptr) return b == nullptr;
      if (b == nullptr) return false;
      return a->pattern() == b->pattern();
    };
    return disallow_all == other.disallow_all &&
           disallow_is_error == other.disallow_is_error &&
           is_re_equal(disallow_expression.get(),
                       other.disallow_expression.get()) &&
           is_re_equal(allow_expression.get(), other.allow_expression.get());
  }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_COMMON_TYPES_H
