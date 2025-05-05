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

#ifndef GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_SPIFFE_UTILS_H
#define GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_SPIFFE_UTILS_H

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_object_loader.h"

namespace grpc_core {

// A representation of a SPIFFE ID per the spec:
// https://github.com/spiffe/spiffe/blob/main/standards/SPIFFE-ID.md#the-spiffe-identity-and-verifiable-identity-document
class SpiffeId final {
 public:
  // Parses the input string as a SPIFFE ID, and returns an error status if the
  // input string is not a valid SPIFFE ID.
  static absl::StatusOr<SpiffeId> FromString(absl::string_view input);
  // Returns the trust domain of the SPIFFE ID
  absl::string_view trust_domain() { return trust_domain_; }
  // Returns the path of the SPIFFE ID
  absl::string_view path() { return path_; }

 private:
  SpiffeId(absl::string_view trust_domain, absl::string_view path)
      : trust_domain_(trust_domain), path_(path) {}
  const std::string trust_domain_;
  const std::string path_;
};

// An entry in the Key vector of a SPIFFE Bundle following these documents:
// https://github.com/spiffe/spiffe/blob/main/standards/SPIFFE_Trust_Domain_and_Bundle.md#3-spiffe-bundles
// https://github.com/grpc/proposal/blob/master/A87-mtls-spiffe-support.md
class SpiffeBundleKey final {
 public:
  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* kLoader =
        JsonObjectLoader<SpiffeBundleKey>()
            .Field("kty", &SpiffeBundleKey::kty_)
            .OptionalField("kid", &SpiffeBundleKey::kid_)
            .Field("use", &SpiffeBundleKey::use_)
            .Field("x5c", &SpiffeBundleKey::x5c_)
            .Field("n", &SpiffeBundleKey::n_)
            .Field("e", &SpiffeBundleKey::e_)
            .Finish();
    return kLoader;
  }

  void JsonPostLoad(const Json& json, const JsonArgs&,
                    ValidationErrors* errors);

  // Returns the PEM x509 string for this SPIFFE Bundle entry which is the only
  // entry in the x5c_ vector.
  absl::StatusOr<absl::string_view> GetRoot();

 private:
  // The following are all fields of the the JWK key in the SPIFFE bundle per
  // https://github.com/spiffe/spiffe/blob/main/standards/SPIFFE_Trust_Domain_and_Bundle.md#3-spiffe-bundles
  std::string kty_;
  std::string kid_;
  std::string use_;
  std::vector<std::string> x5c_;
  std::string n_;
  std::string e_;
};

// A SPIFFE bundle consists of a trust domain and a set of roots for that trust
// domain.
// https://github.com/spiffe/spiffe/blob/main/standards/SPIFFE_Trust_Domain_and_Bundle.md#3-spiffe-bundles
// https://github.com/grpc/proposal/blob/master/A87-mtls-spiffe-support.md
class SpiffeBundle final {
 public:
  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* kLoader = JsonObjectLoader<SpiffeBundle>()
                                     .Field("keys", &SpiffeBundle::keys_)
                                     .Finish();
    return kLoader;
  }

  // Returns a vector of the roots in this SPIFFE Bundle.
  absl::StatusOr<absl::Span<const absl::string_view>> GetRoots();

 private:
  std::vector<SpiffeBundleKey> keys_;
  std::vector<absl::string_view> roots_;
};

// A map of SPIFFE bundles keyed to trust domains. This functions as a map of a
// given trust domain to the root certificates that should be used when
// validating certificates in this trust domain.
// https://github.com/grpc/proposal/blob/master/A87-mtls-spiffe-support.md
// https://github.com/grpc/proposal/blob/master/A87-mtls-spiffe-support.md
// Only configuring X509 roots is supported.
class SpiffeBundleMap final {
 public:
  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* kLoader =
        JsonObjectLoader<SpiffeBundleMap>()
            .Field("trust_domains", &SpiffeBundleMap::temp_bundles_)
            .Finish();
    return kLoader;
  }

  void JsonPostLoad(const Json& json, const JsonArgs&,
                    ValidationErrors* errors);

  // Loads a SPIFFE Bundle Map from a json file representation. Returns a bad
  // status if there is a problem while loading the file and parsing the JSON. A
  // returned value represents a valid and SPIFFE Bundle Map.
  // The only supported use is configuring X509 roots for a given trust domain -
  // no other SPIFFE Bundle configurations are supported.
  static absl::StatusOr<SpiffeBundleMap> FromFile(absl::string_view file_path);

  // Returns the roots for a given trust domain in the SPIFFE Bundle Map.
  absl::StatusOr<absl::Span<const absl::string_view>> GetRoots(
      absl::string_view trust_domain);
  size_t size() { return bundles_.size(); }

 private:
  struct StringCmp {
    using is_transparent = void;
    bool operator()(absl::string_view a, absl::string_view b) const {
      return a < b;
    }
  };

  // JsonObjectLoader cannot parse into a map with a custom comparator, so parse
  // into a map without one, then insert those elements into the map with the
  // custom comparator after parsing. This is a one-time conversion to not have
  // to create strings every look-up.
  std::map<std::string, SpiffeBundle> temp_bundles_;
  std::map<std::string, SpiffeBundle, StringCmp> bundles_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_SPIFFE_UTILS_H