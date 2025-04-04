//
//
// Copyright 2016 gRPC authors.
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

#include "test/core/test_util/fake_transport_credentials.h"

#include <grpc/grpc_security_constants.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/channel_creds_registry.h"
#include "src/core/credentials/transport/security_connector.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/security/security_handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/load_balancing/grpclb/grpclb.h"
#include "src/core/transport/auth_context.h"
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/crash.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/host_port.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/useful.h"
#include "test/core/test_util/fake_transport_security.h"

namespace {

//
// fake_check_peer() -- shared by channel and server security connectors
//

void fake_check_peer(tsi_peer peer,
                     grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                     grpc_closure* on_peer_checked) {
  const char* prop_name;
  grpc_error_handle error;
  *auth_context = nullptr;
  if (peer.property_count != 2) {
    error = GRPC_ERROR_CREATE("Fake peers should only have 2 properties.");
    goto end;
  }
  prop_name = peer.properties[0].name;
  if (prop_name == nullptr ||
      strcmp(prop_name, TSI_CERTIFICATE_TYPE_PEER_PROPERTY) != 0) {
    error = GRPC_ERROR_CREATE(
        absl::StrCat("Unexpected property in fake peer: ",
                     prop_name == nullptr ? "<EMPTY>" : prop_name));
    goto end;
  }
  if (strncmp(peer.properties[0].value.data, TSI_FAKE_CERTIFICATE_TYPE,
              peer.properties[0].value.length) != 0) {
    error = GRPC_ERROR_CREATE("Invalid value for cert type property.");
    goto end;
  }
  prop_name = peer.properties[1].name;
  if (prop_name == nullptr ||
      strcmp(prop_name, TSI_SECURITY_LEVEL_PEER_PROPERTY) != 0) {
    error = GRPC_ERROR_CREATE(
        absl::StrCat("Unexpected property in fake peer: ",
                     prop_name == nullptr ? "<EMPTY>" : prop_name));
    goto end;
  }
  if (strncmp(peer.properties[1].value.data, TSI_FAKE_SECURITY_LEVEL,
              peer.properties[1].value.length) != 0) {
    error = GRPC_ERROR_CREATE("Invalid value for security level property.");
    goto end;
  }
  *auth_context = grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context_add_cstring_property(
      auth_context->get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME, "fake");
  grpc_auth_context_add_cstring_property(
      auth_context->get(), GRPC_TRANSPORT_SECURITY_LEVEL_PROPERTY_NAME,
      TSI_FAKE_SECURITY_LEVEL);
end:
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
  tsi_peer_destruct(&peer);
}

//
// grpc_fake_channel_security_connector
//

class grpc_fake_channel_security_connector final
    : public grpc_channel_security_connector {
 public:
  grpc_fake_channel_security_connector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target, const grpc_core::ChannelArgs& args)
      : grpc_channel_security_connector(GRPC_FAKE_SECURITY_URL_SCHEME,
                                        std::move(channel_creds),
                                        std::move(request_metadata_creds)),
        target_(gpr_strdup(target)),
        expected_targets_(
            args.GetOwnedString(GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS)),
        is_lb_channel_(args.GetBool(GRPC_ARG_ADDRESS_IS_GRPCLB_LOAD_BALANCER)
                           .value_or(false)),
        target_name_override_(
            args.GetOwnedString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG)) {}

  ~grpc_fake_channel_security_connector() override { gpr_free(target_); }

  void check_peer(tsi_peer peer, grpc_endpoint* /*ep*/,
                  const grpc_core::ChannelArgs& /*args*/,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    fake_check_peer(peer, auth_context, on_peer_checked);
    fake_secure_name_check();
  }

  void cancel_check_peer(grpc_closure* /*on_peer_checked*/,
                         grpc_error_handle /*error*/) override {}

  int cmp(const grpc_security_connector* other_sc) const override {
    auto* other =
        reinterpret_cast<const grpc_fake_channel_security_connector*>(other_sc);
    int c = channel_security_connector_cmp(other);
    if (c != 0) return c;
    c = strcmp(target_, other->target_);
    if (c != 0) return c;
    c = grpc_core::QsortCompare(expected_targets_, other->expected_targets_);
    if (c != 0) return c;
    return grpc_core::QsortCompare(is_lb_channel_, other->is_lb_channel_);
  }

  void add_handshakers(const grpc_core::ChannelArgs& args,
                       grpc_pollset_set* /*interested_parties*/,
                       grpc_core::HandshakeManager* handshake_mgr) override {
    handshake_mgr->Add(grpc_core::SecurityHandshakerCreate(
        tsi_create_fake_handshaker(/*is_client=*/true), this, args));
  }

  grpc_core::ArenaPromise<absl::Status> CheckCallHost(
      absl::string_view host, grpc_auth_context*) override {
    absl::string_view authority_hostname;
    absl::string_view authority_ignored_port;
    absl::string_view target_hostname;
    absl::string_view target_ignored_port;
    grpc_core::SplitHostPort(host, &authority_hostname,
                             &authority_ignored_port);
    grpc_core::SplitHostPort(target_, &target_hostname, &target_ignored_port);
    if (target_name_override_.has_value()) {
      absl::string_view fake_security_target_name_override_hostname;
      absl::string_view fake_security_target_name_override_ignored_port;
      grpc_core::SplitHostPort(
          target_name_override_->c_str(),
          &fake_security_target_name_override_hostname,
          &fake_security_target_name_override_ignored_port);
      if (authority_hostname != fake_security_target_name_override_hostname) {
        grpc_core::Crash(absl::StrFormat(
            "Authority (host) '%s' != Fake Security Target override '%s'",
            host.data(), fake_security_target_name_override_hostname.data()));
      }
    } else if (authority_hostname != target_hostname) {
      grpc_core::Crash(absl::StrFormat("Authority (host) '%s' != Target '%s'",
                                       host.data(), target_));
    }
    return grpc_core::ImmediateOkStatus();
  }

  char* target() const { return target_; }
  bool is_lb_channel() const { return is_lb_channel_; }

 private:
  bool fake_check_target(const char* target, const char* set_str) const {
    CHECK_NE(target, nullptr);
    char** set = nullptr;
    size_t set_size = 0;
    gpr_string_split(set_str, ",", &set, &set_size);
    bool found = false;
    for (size_t i = 0; i < set_size; ++i) {
      if (set[i] != nullptr && strcmp(target, set[i]) == 0) found = true;
    }
    for (size_t i = 0; i < set_size; ++i) {
      gpr_free(set[i]);
    }
    gpr_free(set);
    return found;
  }

  void fake_secure_name_check() const {
    if (!expected_targets_.has_value()) return;
    char** lbs_and_backends = nullptr;
    size_t lbs_and_backends_size = 0;
    bool success = false;
    gpr_string_split(expected_targets_->c_str(), ";", &lbs_and_backends,
                     &lbs_and_backends_size);
    if (lbs_and_backends_size > 2 || lbs_and_backends_size == 0) {
      LOG(ERROR) << "Invalid expected targets arg value: '"
                 << expected_targets_->c_str() << "'";
      goto done;
    }
    if (is_lb_channel_) {
      if (lbs_and_backends_size != 2) {
        LOG(ERROR) << "Invalid expected targets arg value: '"
                   << expected_targets_->c_str()
                   << "'. Expectations for LB channels must be of the form "
                      "'be1,be2,be3,...;lb1,lb2,...";
        goto done;
      }
      if (!fake_check_target(target_, lbs_and_backends[1])) {
        LOG(ERROR) << "LB target '" << target_
                   << "' not found in expected set '" << lbs_and_backends[1]
                   << "'";
        goto done;
      }
      success = true;
    } else {
      if (!fake_check_target(target_, lbs_and_backends[0])) {
        LOG(ERROR) << "Backend target '" << target_
                   << "' not found in expected set '" << lbs_and_backends[0]
                   << "'";
        goto done;
      }
      success = true;
    }
  done:
    for (size_t i = 0; i < lbs_and_backends_size; ++i) {
      gpr_free(lbs_and_backends[i]);
    }
    gpr_free(lbs_and_backends);
    if (!success) abort();
  }

  char* target_;
  std::optional<std::string> expected_targets_;
  bool is_lb_channel_;
  std::optional<std::string> target_name_override_;
};

}  // namespace

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_fake_channel_security_connector_create(
    grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
    grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const char* target, const grpc_core::ChannelArgs& args) {
  return grpc_core::MakeRefCounted<grpc_fake_channel_security_connector>(
      std::move(channel_creds), std::move(request_metadata_creds), target,
      args);
}

//
// grpc_fake_server_security_connector
//

namespace {

class grpc_fake_server_security_connector
    : public grpc_server_security_connector {
 public:
  explicit grpc_fake_server_security_connector(
      grpc_core::RefCountedPtr<grpc_server_credentials> server_creds)
      : grpc_server_security_connector(GRPC_FAKE_SECURITY_URL_SCHEME,
                                       std::move(server_creds)) {}
  ~grpc_fake_server_security_connector() override = default;

  void check_peer(tsi_peer peer, grpc_endpoint* /*ep*/,
                  const grpc_core::ChannelArgs& /*args*/,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    fake_check_peer(peer, auth_context, on_peer_checked);
  }

  void cancel_check_peer(grpc_closure* /*on_peer_checked*/,
                         grpc_error_handle /*error*/) override {}

  void add_handshakers(const grpc_core::ChannelArgs& args,
                       grpc_pollset_set* /*interested_parties*/,
                       grpc_core::HandshakeManager* handshake_mgr) override {
    handshake_mgr->Add(grpc_core::SecurityHandshakerCreate(
        tsi_create_fake_handshaker(/*=is_client*/ false), this, args));
  }

  int cmp(const grpc_security_connector* other) const override {
    return server_security_connector_cmp(
        static_cast<const grpc_server_security_connector*>(other));
  }
};

}  // namespace

//
// grpc_fake_channel_credentials
//

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_fake_channel_credentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
    const char* target, grpc_core::ChannelArgs* args) {
  return grpc_fake_channel_security_connector_create(
      this->Ref(), std::move(call_creds), target, *args);
}

grpc_core::UniqueTypeName grpc_fake_channel_credentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("Fake");
  return kFactory.Create();
}

int grpc_fake_channel_credentials::cmp_impl(
    const grpc_channel_credentials* other) const {
  // TODO(yashykt): Check if we can do something better here
  return grpc_core::QsortCompare(
      static_cast<const grpc_channel_credentials*>(this), other);
}

grpc_channel_credentials* grpc_fake_transport_security_credentials_create() {
  return new grpc_fake_channel_credentials();
}

//
// grpc_fake_server_credentials
//

grpc_core::RefCountedPtr<grpc_server_security_connector>
grpc_fake_server_credentials::create_security_connector(
    const grpc_core::ChannelArgs& /*args*/) {
  return grpc_core::MakeRefCounted<grpc_fake_server_security_connector>(
      this->Ref());
}

grpc_core::UniqueTypeName grpc_fake_server_credentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("Fake");
  return kFactory.Create();
}

grpc_server_credentials*
grpc_fake_transport_security_server_credentials_create() {
  return new grpc_fake_server_credentials();
}

//
// channel creds registration
//

namespace grpc_core {

namespace {

class FakeChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }
  RefCountedPtr<ChannelCredsConfig> ParseConfig(
      const Json& /*config*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<ChannelCredsConfig> /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_fake_transport_security_credentials_create());
  }

 private:
  class Config : public ChannelCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }
    bool Equals(const ChannelCredsConfig&) const override { return true; }
    std::string ToString() const override { return "{}"; }
  };

  static absl::string_view Type() { return "fake"; }
};

}  // namespace

void RegisterFakeChannelCredentialsBuilder() {
  CoreConfiguration::RegisterBuilder([](CoreConfiguration::Builder* builder) {
    builder->channel_creds_registry()->RegisterChannelCredsFactory(
        std::make_unique<FakeChannelCredsFactory>());
  });
}

}  // namespace grpc_core
