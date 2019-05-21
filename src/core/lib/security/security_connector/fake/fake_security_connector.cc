/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/security_connector/fake/fake_security_connector.h"

#include <stdbool.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds.h"
#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/security/transport/target_authority_table.h"
#include "src/core/tsi/fake_transport_security.h"

namespace {
class grpc_fake_channel_security_connector final
    : public grpc_channel_security_connector {
 public:
  grpc_fake_channel_security_connector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target, const grpc_channel_args* args)
      : grpc_channel_security_connector(GRPC_FAKE_SECURITY_URL_SCHEME,
                                        std::move(channel_creds),
                                        std::move(request_metadata_creds)),
        target_(gpr_strdup(target)),
        expected_targets_(
            gpr_strdup(grpc_fake_transport_get_expected_targets(args))),
        is_lb_channel_(
            grpc_channel_args_find(
                args, GRPC_ARG_ADDRESS_IS_XDS_LOAD_BALANCER) != nullptr ||
            grpc_channel_args_find(
                args, GRPC_ARG_ADDRESS_IS_GRPCLB_LOAD_BALANCER) != nullptr) {
    const grpc_arg* target_name_override_arg =
        grpc_channel_args_find(args, GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
    if (target_name_override_arg != nullptr) {
      target_name_override_ =
          gpr_strdup(grpc_channel_arg_get_string(target_name_override_arg));
    } else {
      target_name_override_ = nullptr;
    }
  }

  ~grpc_fake_channel_security_connector() override {
    gpr_free(target_);
    gpr_free(expected_targets_);
    if (target_name_override_ != nullptr) gpr_free(target_name_override_);
  }

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override;

  int cmp(const grpc_security_connector* other_sc) const override {
    auto* other =
        reinterpret_cast<const grpc_fake_channel_security_connector*>(other_sc);
    int c = channel_security_connector_cmp(other);
    if (c != 0) return c;
    c = strcmp(target_, other->target_);
    if (c != 0) return c;
    if (expected_targets_ == nullptr || other->expected_targets_ == nullptr) {
      c = GPR_ICMP(expected_targets_, other->expected_targets_);
    } else {
      c = strcmp(expected_targets_, other->expected_targets_);
    }
    if (c != 0) return c;
    return GPR_ICMP(is_lb_channel_, other->is_lb_channel_);
  }

  void add_handshakers(grpc_pollset_set* interested_parties,
                       grpc_core::HandshakeManager* handshake_mgr) override {
    handshake_mgr->Add(grpc_core::SecurityHandshakerCreate(
        tsi_create_fake_handshaker(/*is_client=*/true), this));
  }

  bool check_call_host(const char* host, grpc_auth_context* auth_context,
                       grpc_closure* on_call_host_checked,
                       grpc_error** error) override {
    char* authority_hostname = nullptr;
    char* authority_ignored_port = nullptr;
    char* target_hostname = nullptr;
    char* target_ignored_port = nullptr;
    gpr_split_host_port(host, &authority_hostname, &authority_ignored_port);
    gpr_split_host_port(target_, &target_hostname, &target_ignored_port);
    if (target_name_override_ != nullptr) {
      char* fake_security_target_name_override_hostname = nullptr;
      char* fake_security_target_name_override_ignored_port = nullptr;
      gpr_split_host_port(target_name_override_,
                          &fake_security_target_name_override_hostname,
                          &fake_security_target_name_override_ignored_port);
      if (strcmp(authority_hostname,
                 fake_security_target_name_override_hostname) != 0) {
        gpr_log(GPR_ERROR,
                "Authority (host) '%s' != Fake Security Target override '%s'",
                host, fake_security_target_name_override_hostname);
        abort();
      }
      gpr_free(fake_security_target_name_override_hostname);
      gpr_free(fake_security_target_name_override_ignored_port);
    } else if (strcmp(authority_hostname, target_hostname) != 0) {
      gpr_log(GPR_ERROR, "Authority (host) '%s' != Target '%s'",
              authority_hostname, target_hostname);
      abort();
    }
    gpr_free(authority_hostname);
    gpr_free(authority_ignored_port);
    gpr_free(target_hostname);
    gpr_free(target_ignored_port);
    return true;
  }

  void cancel_check_call_host(grpc_closure* on_call_host_checked,
                              grpc_error* error) override {
    GRPC_ERROR_UNREF(error);
  }

  char* target() const { return target_; }
  char* expected_targets() const { return expected_targets_; }
  bool is_lb_channel() const { return is_lb_channel_; }
  char* target_name_override() const { return target_name_override_; }

 private:
  bool fake_check_target(const char* target_type, const char* target,
                         const char* set_str) const {
    GPR_ASSERT(target_type != nullptr);
    GPR_ASSERT(target != nullptr);
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
    if (expected_targets_ == nullptr) return;
    char** lbs_and_backends = nullptr;
    size_t lbs_and_backends_size = 0;
    bool success = false;
    gpr_string_split(expected_targets_, ";", &lbs_and_backends,
                     &lbs_and_backends_size);
    if (lbs_and_backends_size > 2 || lbs_and_backends_size == 0) {
      gpr_log(GPR_ERROR, "Invalid expected targets arg value: '%s'",
              expected_targets_);
      goto done;
    }
    if (is_lb_channel_) {
      if (lbs_and_backends_size != 2) {
        gpr_log(GPR_ERROR,
                "Invalid expected targets arg value: '%s'. Expectations for LB "
                "channels must be of the form 'be1,be2,be3,...;lb1,lb2,...",
                expected_targets_);
        goto done;
      }
      if (!fake_check_target("LB", target_, lbs_and_backends[1])) {
        gpr_log(GPR_ERROR, "LB target '%s' not found in expected set '%s'",
                target_, lbs_and_backends[1]);
        goto done;
      }
      success = true;
    } else {
      if (!fake_check_target("Backend", target_, lbs_and_backends[0])) {
        gpr_log(GPR_ERROR, "Backend target '%s' not found in expected set '%s'",
                target_, lbs_and_backends[0]);
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
  char* expected_targets_;
  bool is_lb_channel_;
  char* target_name_override_;
};

static void fake_check_peer(
    grpc_security_connector* sc, tsi_peer peer,
    grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  const char* prop_name;
  grpc_error* error = GRPC_ERROR_NONE;
  *auth_context = nullptr;
  if (peer.property_count != 1) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Fake peers should only have 1 property.");
    goto end;
  }
  prop_name = peer.properties[0].name;
  if (prop_name == nullptr ||
      strcmp(prop_name, TSI_CERTIFICATE_TYPE_PEER_PROPERTY)) {
    char* msg;
    gpr_asprintf(&msg, "Unexpected property in fake peer: %s.",
                 prop_name == nullptr ? "<EMPTY>" : prop_name);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    goto end;
  }
  if (strncmp(peer.properties[0].value.data, TSI_FAKE_CERTIFICATE_TYPE,
              peer.properties[0].value.length)) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Invalid value for cert type property.");
    goto end;
  }
  *auth_context = grpc_core::MakeRefCounted<grpc_auth_context>(nullptr);
  grpc_auth_context_add_cstring_property(
      auth_context->get(), GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
      GRPC_FAKE_TRANSPORT_SECURITY_TYPE);
end:
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
  tsi_peer_destruct(&peer);
}

void grpc_fake_channel_security_connector::check_peer(
    tsi_peer peer, grpc_endpoint* ep,
    grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  fake_check_peer(this, peer, auth_context, on_peer_checked);
  fake_secure_name_check();
}

class grpc_fake_server_security_connector
    : public grpc_server_security_connector {
 public:
  grpc_fake_server_security_connector(
      grpc_core::RefCountedPtr<grpc_server_credentials> server_creds)
      : grpc_server_security_connector(GRPC_FAKE_SECURITY_URL_SCHEME,
                                       std::move(server_creds)) {}
  ~grpc_fake_server_security_connector() override = default;

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    fake_check_peer(this, peer, auth_context, on_peer_checked);
  }

  void add_handshakers(grpc_pollset_set* interested_parties,
                       grpc_core::HandshakeManager* handshake_mgr) override {
    handshake_mgr->Add(grpc_core::SecurityHandshakerCreate(
        tsi_create_fake_handshaker(/*=is_client*/ false), this));
  }

  int cmp(const grpc_security_connector* other) const override {
    return server_security_connector_cmp(
        static_cast<const grpc_server_security_connector*>(other));
  }
};
}  // namespace

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_fake_channel_security_connector_create(
    grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
    grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const char* target, const grpc_channel_args* args) {
  return grpc_core::MakeRefCounted<grpc_fake_channel_security_connector>(
      std::move(channel_creds), std::move(request_metadata_creds), target,
      args);
}

grpc_core::RefCountedPtr<grpc_server_security_connector>
grpc_fake_server_security_connector_create(
    grpc_core::RefCountedPtr<grpc_server_credentials> server_creds) {
  return grpc_core::MakeRefCounted<grpc_fake_server_security_connector>(
      std::move(server_creds));
}
