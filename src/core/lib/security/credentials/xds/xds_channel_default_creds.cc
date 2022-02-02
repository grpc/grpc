//
//
// Copyright 2022 gRPC authors.
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

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"

namespace grpc_core {

class GoogleDefaultXdsChannelCredsFactory : public XdsChannelCredsFactory<> {
 public:
  absl::string_view creds_type() const override { return "google_default"; }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
  RefCountedPtr<grpc_channel_credentials> CreateXdsChannelCreds(
      const Json& /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_google_default_credentials_create(nullptr));
  }
};

class InsecureXdsChannelCredsFactory : public XdsChannelCredsFactory<> {
 public:
  absl::string_view creds_type() const override { return "insecure"; }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
  RefCountedPtr<grpc_channel_credentials> CreateXdsChannelCreds(
      const Json& /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  }
};

class FakeXdsChannelCredsFactory : public XdsChannelCredsFactory<> {
 public:
  absl::string_view creds_type() const override { return "fake"; }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
  RefCountedPtr<grpc_channel_credentials> CreateXdsChannelCreds(
      const Json& /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_fake_transport_security_credentials_create());
  }
};

void RegisterXdsChannelDefaultCreds(CoreConfiguration::Builder* builder) {
  builder->xds_channel_creds_registry()->RegisterXdsChannelCredsFactory(
      absl::make_unique<GoogleDefaultXdsChannelCredsFactory>());
  builder->xds_channel_creds_registry()->RegisterXdsChannelCredsFactory(
      absl::make_unique<InsecureXdsChannelCredsFactory>());
  builder->xds_channel_creds_registry()->RegisterXdsChannelCredsFactory(
      absl::make_unique<FakeXdsChannelCredsFactory>());
}

}  // namespace grpc_core
