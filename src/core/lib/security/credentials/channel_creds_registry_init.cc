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

#include <memory>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/channel_creds_registry.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"

namespace grpc_core {

class GoogleDefaultChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view creds_type() const override { return "google_default"; }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      const Json& /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_google_default_credentials_create(nullptr));
  }
};

class InsecureChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view creds_type() const override { return "insecure"; }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      const Json& /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  }
};

class FakeChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view creds_type() const override { return "fake"; }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      const Json& /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_fake_transport_security_credentials_create());
  }
};

void RegisterChannelDefaultCreds(CoreConfiguration::Builder* builder) {
  builder->channel_creds_registry()->RegisterChannelCredsFactory(
      absl::make_unique<GoogleDefaultChannelCredsFactory>());
  builder->channel_creds_registry()->RegisterChannelCredsFactory(
      absl::make_unique<InsecureChannelCredsFactory>());
  builder->channel_creds_registry()->RegisterChannelCredsFactory(
      absl::make_unique<FakeChannelCredsFactory>());
}

}  // namespace grpc_core
