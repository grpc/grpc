//
// Copyright 2019 gRPC authors.
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

#include "src/core/ext/xds/xds_channel_creds.h"

#include "src/core/lib/security/credentials/fake/fake_credentials.h"

namespace grpc_core {

namespace {

using ChannelCredsMap =
    std::map<absl::string_view, std::unique_ptr<XdsChannelCredsImpl>>;
ChannelCredsMap* g_creds = nullptr;

}  // namespace

//
// XdsChannelCredsImpl implementations for default-supported cred types.
//

class GoogleDefaultXdsChannelCredsImpl : public XdsChannelCredsImpl {
 public:
  absl::string_view creds_type() const override { return "google_default"; }
  RefCountedPtr<grpc_channel_credentials> CreateXdsChannelCreds(
      const Json& /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_google_default_credentials_create(nullptr));
  }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
};

class InsecureXdsChannelCredsImpl : public XdsChannelCredsImpl {
 public:
  absl::string_view creds_type() const override { return "insecure"; }
  RefCountedPtr<grpc_channel_credentials> CreateXdsChannelCreds(
      const Json& /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
};

class FakeXdsChannelCredsImpl : public XdsChannelCredsImpl {
 public:
  absl::string_view creds_type() const override { return "fake"; }
  RefCountedPtr<grpc_channel_credentials> CreateXdsChannelCreds(
      const Json& /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_fake_transport_security_credentials_create());
  }
  bool IsValidConfig(const Json& /*config*/) const override { return true; }
};

//
// XdsChannelCredsRegistry
//

bool XdsChannelCredsRegistry::IsSupported(const std::string& creds_type) {
  return g_creds->find(creds_type) != g_creds->end();
}

bool XdsChannelCredsRegistry::IsValidConfig(const std::string& creds_type,
                                            const Json& config) {
  const auto iter = g_creds->find(creds_type);
  if (iter == g_creds->cend()) return false;
  return iter->second->IsValidConfig(config);
}

RefCountedPtr<grpc_channel_credentials>
XdsChannelCredsRegistry::CreateXdsChannelCreds(const std::string& creds_type,
                                               const Json& config) {
  const auto iter = g_creds->find(creds_type);
  if (iter == g_creds->cend()) return nullptr;
  return iter->second->CreateXdsChannelCreds(config);
}

void XdsChannelCredsRegistry::Init() {
  g_creds = new ChannelCredsMap();
  RegisterXdsChannelCreds(
      absl::make_unique<GoogleDefaultXdsChannelCredsImpl>());
  RegisterXdsChannelCreds(absl::make_unique<InsecureXdsChannelCredsImpl>());
  RegisterXdsChannelCreds(absl::make_unique<FakeXdsChannelCredsImpl>());
}

void XdsChannelCredsRegistry::Shutdown() { delete g_creds; }

void XdsChannelCredsRegistry::RegisterXdsChannelCreds(
    std::unique_ptr<XdsChannelCredsImpl> creds) {
  (*g_creds)[creds->creds_type()] = std::move(creds);
}

}  // namespace grpc_core
