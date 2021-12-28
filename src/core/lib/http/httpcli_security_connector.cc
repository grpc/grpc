/*
 *
 * Copyright 2015 gRPC authors.
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

#include <string.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/ssl_transport_security.h"

class grpc_httpcli_ssl_channel_security_connector final
    : public grpc_channel_security_connector {
 public:
  explicit grpc_httpcli_ssl_channel_security_connector(char* secure_peer_name)
      : grpc_channel_security_connector(
            /*url_scheme=*/nullptr,
            /*channel_creds=*/nullptr,
            /*request_metadata_creds=*/nullptr),
        secure_peer_name_(secure_peer_name) {}

  ~grpc_httpcli_ssl_channel_security_connector() override {
    if (handshaker_factory_ != nullptr) {
      tsi_ssl_client_handshaker_factory_unref(handshaker_factory_);
    }
    if (secure_peer_name_ != nullptr) {
      gpr_free(secure_peer_name_);
    }
  }

  tsi_result InitHandshakerFactory(const char* pem_root_certs,
                                   const tsi_ssl_root_certs_store* root_store) {
    tsi_ssl_client_handshaker_options options;
    options.pem_root_certs = pem_root_certs;
    options.root_store = root_store;
    return tsi_create_ssl_client_handshaker_factory_with_options(
        &options, &handshaker_factory_);
  }

  void add_handshakers(const grpc_channel_args* args,
                       grpc_pollset_set* /*interested_parties*/,
                       grpc_core::HandshakeManager* handshake_mgr) override {
    tsi_handshaker* handshaker = nullptr;
    if (handshaker_factory_ != nullptr) {
      tsi_result result = tsi_ssl_client_handshaker_factory_create_handshaker(
          handshaker_factory_, secure_peer_name_, &handshaker);
      if (result != TSI_OK) {
        gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
                tsi_result_to_string(result));
      }
    }
    handshake_mgr->Add(
        grpc_core::SecurityHandshakerCreate(handshaker, this, args));
  }

  tsi_ssl_client_handshaker_factory* handshaker_factory() const {
    return handshaker_factory_;
  }

  void check_peer(tsi_peer peer, grpc_endpoint* /*ep*/,
                  grpc_core::RefCountedPtr<grpc_auth_context>* /*auth_context*/,
                  grpc_closure* on_peer_checked) override {
    grpc_error_handle error = GRPC_ERROR_NONE;

    /* Check the peer name. */
    if (secure_peer_name_ != nullptr &&
        !tsi_ssl_peer_matches_name(&peer, secure_peer_name_)) {
      error = GRPC_ERROR_CREATE_FROM_CPP_STRING(absl::StrCat(
          "Peer name ", secure_peer_name_, " is not in peer certificate"));
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
    tsi_peer_destruct(&peer);
  }

  void cancel_check_peer(grpc_closure* /*on_peer_checked*/,
                         grpc_error_handle error) override {
    GRPC_ERROR_UNREF(error);
  }

  int cmp(const grpc_security_connector* other_sc) const override {
    auto* other =
        reinterpret_cast<const grpc_httpcli_ssl_channel_security_connector*>(
            other_sc);
    return strcmp(secure_peer_name_, other->secure_peer_name_);
  }

  bool check_call_host(absl::string_view /*host*/,
                       grpc_auth_context* /*auth_context*/,
                       grpc_closure* /*on_call_host_checked*/,
                       grpc_error_handle* error) override {
    *error = GRPC_ERROR_NONE;
    return true;
  }

  void cancel_check_call_host(grpc_closure* /*on_call_host_checked*/,
                              grpc_error_handle error) override {
    GRPC_ERROR_UNREF(error);
  }

  const char* secure_peer_name() const { return secure_peer_name_; }

 private:
  tsi_ssl_client_handshaker_factory* handshaker_factory_ = nullptr;
  char* secure_peer_name_;
};

static grpc_core::RefCountedPtr<grpc_channel_security_connector>
httpcli_ssl_channel_security_connector_create(
    const char* pem_root_certs, const tsi_ssl_root_certs_store* root_store,
    const char* secure_peer_name) {
  if (secure_peer_name != nullptr && pem_root_certs == nullptr) {
    gpr_log(GPR_ERROR,
            "Cannot assert a secure peer name without a trust root.");
    return nullptr;
  }
  grpc_core::RefCountedPtr<grpc_httpcli_ssl_channel_security_connector> c =
      grpc_core::MakeRefCounted<grpc_httpcli_ssl_channel_security_connector>(
          secure_peer_name == nullptr ? nullptr : gpr_strdup(secure_peer_name));
  tsi_result result = c->InitHandshakerFactory(pem_root_certs, root_store);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
            tsi_result_to_string(result));
    return nullptr;
  }
  return c;
}

/* handshaker */

namespace grpc_core {

void HttpCli::SSLHttpCliHandshaker::InnerOnDone(void* arg,
                                                grpc_error_handle error) {
  auto* args = static_cast<HandshakerArgs*>(arg);
  auto* self = static_cast<HttpCli::SSLHttpCliHandshaker*>(args->user_data);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Secure transport setup failed: %s",
            grpc_error_std_string(error).c_str());
    self->on_done_(nullptr);
  } else {
    grpc_channel_args_destroy(args->args);
    grpc_slice_buffer_destroy_internal(args->read_buffer);
    gpr_free(args->read_buffer);
    self->on_done_(args->endpoint);
  }
  self->Unref();
}

void HttpCli::SSLHttpCliHandshaker::Start() {
  const char* pem_root_certs =
      DefaultSslRootStore::GetPemRootCerts();
  const tsi_ssl_root_certs_store* root_store =
      DefaultSslRootStore::GetRootStore();
  if (root_store == nullptr) {
    gpr_log(GPR_ERROR, "Could not get default pem root certs.");
    on_done_(nullptr);
    return;
  }
  RefCountedPtr<grpc_channel_security_connector> sc =
      httpcli_ssl_channel_security_connector_create(pem_root_certs, root_store,
                                                    host_.c_str());
  GPR_ASSERT(sc != nullptr);
  grpc_arg channel_arg = grpc_security_connector_to_arg(sc.get());
  grpc_channel_args args = {1, &channel_arg};
  handshake_mgr_ = MakeRefCounted<HandshakeManager>();
  CoreConfiguration::Get().handshaker_registry().AddHandshakers(
      HANDSHAKER_CLIENT, &args,
      /*interested_parties=*/nullptr, handshake_mgr_.get());
  Ref().release();  // ref held by pending handshake
  handshake_mgr_->DoHandshake(
      original_endpoint_, /*channel_args=*/nullptr, deadline_,
      /*acceptor=*/nullptr, HttpCli::SSLHttpCliHandshaker::InnerOnDone,
      /*user_data=*/this);
  sc.reset(DEBUG_LOCATION, "httpcli");
}

void HttpCli::SSLHttpCliHandshaker::Orphan() {
  if (handshake_mgr_ != nullptr) {
    handshake_mgr_->Shutdown(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "HttpCli::SSLHttpCliHandshaker::Orphan"));
  }
}

}  // namespace grpc_core
