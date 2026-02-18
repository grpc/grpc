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

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
  void JsonPostLoad(const Json& json, const JsonArgs& args,
                    ValidationErrors* errors);

  std::string ToJsonString() const;
};

struct HeaderValueOption {
  struct HeaderValue {
    // Header name.
    std::string key;
    // Only one of ``value`` or ``raw_value`` can be set.
    // Header value is encoded as string. This does not work for non-utf8
    // characters.
    std::string value;
  };

  enum class AppendAction {
    // If the header doesn't exist then this will add new header with
    // specified key and value.
    kAppendIfExistsOrAdd = 0,
    // This action will add the header if it doesn't already exist. If the
    // header
    // already exists then this will be a no-op.
    kAddIfAbsent = 1,
    // This action will overwrite the specified value by discarding any
    // existing values if
    // the header already exists. If the header doesn't exist then this will
    // add the header
    // with specified key and value.
    kOverwriteIfExistsOrAdd = 2,
    // This action will overwrite the specified value by discarding any
    // existing values if
    // the header already exists. If the header doesn't exist then this will
    // be no-op.
    kOverwriteIfExists = 3,
    // Default if not specified
    kDefault = kAppendIfExistsOrAdd
  };

  // Header name/value pair that this option applies to
  HeaderValue header;
  // Describes the action taken to append/overwrite the given value for an
  // existing header
  // or to only add this header if it's absent.
  // Value defaults to :ref:`APPEND_IF_EXISTS_OR_ADD
  // <envoy_v3_api_enum_value_config.core.v3.HeaderValueOption.HeaderAppendAction.APPEND_IF_EXISTS_OR_ADD>`.
  AppendAction append_action;
  // Is the header value allowed to be empty? If false (default), custom
  // headers with empty values are dropped, otherwise they are added.
  bool keep_empty_value;
};

struct SafeRegexMatch {
  std::string regex;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
};

struct StringMatch {
  StringMatcher matcher;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
  void JsonPostLoad(const Json& json, const JsonArgs& args,
                    ValidationErrors* errors);
};

struct HeaderMutationRules {
  bool disallow_all;
  bool disallow_is_error;
  StringMatcher allow_expression;
  StringMatcher disallow_expression;

  std::string ToJsonString() const;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
  void JsonPostLoad(const Json& json, const JsonArgs& args,
                    ValidationErrors* errors);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_COMMON_TYPES_H
