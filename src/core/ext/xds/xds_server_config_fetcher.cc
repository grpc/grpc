//
//
// Copyright 2020 gRPC authors.
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

#include "src/core/ext/xds/xds_certificate_provider.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/credentials/xds/xds_credentials.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

namespace grpc_core {

TraceFlag grpc_xds_server_config_fetcher_trace(false,
                                               "xds_server_config_fetcher");

namespace {

class XdsServerConfigFetcher : public grpc_server_config_fetcher {
 public:
  explicit XdsServerConfigFetcher(RefCountedPtr<XdsClient> xds_client)
      : xds_client_(std::move(xds_client)) {
    GPR_ASSERT(xds_client_ != nullptr);
  }

  void StartWatch(std::string listening_address, grpc_channel_args* args,
                  std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
                      watcher) override {
    grpc_server_config_fetcher::WatcherInterface* watcher_ptr = watcher.get();
    auto listener_watcher = absl::make_unique<ListenerWatcher>(
        std::move(watcher), args, xds_client_);
    auto* listener_watcher_ptr = listener_watcher.get();
    // TODO(yashykt): Get the resource name id from bootstrap
    listening_address = absl::StrCat(
        "grpc/server?xds.resource.listening_address=", listening_address);
    xds_client_->WatchListenerData(listening_address,
                                   std::move(listener_watcher));
    MutexLock lock(&mu_);
    auto& watcher_state = watchers_[watcher_ptr];
    watcher_state.listening_address = listening_address;
    watcher_state.listener_watcher = listener_watcher_ptr;
  }

  void CancelWatch(
      grpc_server_config_fetcher::WatcherInterface* watcher) override {
    MutexLock lock(&mu_);
    auto it = watchers_.find(watcher);
    if (it != watchers_.end()) {
      // Cancel the watch on the listener before erasing
      xds_client_->CancelListenerDataWatch(it->second.listening_address,
                                           it->second.listener_watcher,
                                           false /* delay_unsubscription */);
      watchers_.erase(it);
    }
  }

  // Return the interested parties from the xds client so that it can be polled.
  grpc_pollset_set* interested_parties() override {
    return xds_client_->interested_parties();
  }

 private:
  class ListenerWatcher : public XdsClient::ListenerWatcherInterface {
   public:
    explicit ListenerWatcher(
        std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
            server_config_watcher,
        grpc_channel_args* args, RefCountedPtr<XdsClient> xds_client)
        : server_config_watcher_(std::move(server_config_watcher)),
          args_(args),
          xds_client_(std::move(xds_client)) {}

    ~ListenerWatcher() override { grpc_channel_args_destroy(args_); }

    // Deleted due to special handling required for args_. Copy the channel args
    // if we ever need these.
    ListenerWatcher(const ListenerWatcher&) = delete;
    ListenerWatcher& operator=(const ListenerWatcher&) = delete;

    void OnListenerChanged(XdsApi::LdsUpdate listener) override {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_server_config_fetcher_trace)) {
        gpr_log(
            GPR_INFO,
            "[ListenerWatcher %p] Received LDS update from xds client %p: %s",
            this, xds_client_.get(), listener.ToString().c_str());
      }
      grpc_error* error = GRPC_ERROR_NONE;
      bool update_needed = UpdateXdsCertificateProvider(listener, &error);
      if (error != GRPC_ERROR_NONE) {
        OnError(error);
        return;
      }
      // Only send an update, if something changed.
      if (updated_once_ && !update_needed) {
        return;
      }
      updated_once_ = true;
      grpc_channel_args* updated_args = nullptr;
      if (xds_certificate_provider_ != nullptr) {
        grpc_arg arg_to_add = xds_certificate_provider_->MakeChannelArg();
        updated_args = grpc_channel_args_copy_and_add(args_, &arg_to_add, 1);
      } else {
        updated_args = grpc_channel_args_copy(args_);
      }
      server_config_watcher_->UpdateConfig(updated_args);
    }

    void OnError(grpc_error* error) override {
      gpr_log(GPR_ERROR, "ListenerWatcher:%p XdsClient reports error: %s", this,
              grpc_error_string(error));
      GRPC_ERROR_UNREF(error);
      // TODO(yashykt): We might want to bubble this error to the application.
    }

    void OnResourceDoesNotExist() override {
      gpr_log(GPR_ERROR,
              "ListenerWatcher:%p XdsClient reports requested listener does "
              "not exist",
              this);
      // TODO(yashykt): We might want to bubble this error to the application.
    }

   private:
    // Returns true if the xds certificate provider changed in a way that
    // required a new security connector to be created, false otherwise.
    bool UpdateXdsCertificateProvider(const XdsApi::LdsUpdate& listener,
                                      grpc_error** error) {
      // Early out if channel is not configured to use xDS security.
      grpc_server_credentials* server_creds =
          grpc_find_server_credentials_in_args(args_);
      if (server_creds == nullptr ||
          server_creds->type() != kCredentialsTypeXds) {
        xds_certificate_provider_ = nullptr;
        return false;
      }
      if (xds_certificate_provider_ == nullptr) {
        xds_certificate_provider_ = MakeRefCounted<XdsCertificateProvider>();
      }
      // Configure root cert.
      absl::string_view root_provider_instance_name =
          listener.downstream_tls_context.common_tls_context
              .combined_validation_context
              .validation_context_certificate_provider_instance.instance_name;
      absl::string_view root_provider_cert_name =
          listener.downstream_tls_context.common_tls_context
              .combined_validation_context
              .validation_context_certificate_provider_instance
              .certificate_name;
      RefCountedPtr<grpc_tls_certificate_provider> new_root_provider;
      if (!root_provider_instance_name.empty()) {
        new_root_provider =
            xds_client_->certificate_provider_store()
                .CreateOrGetCertificateProvider(root_provider_instance_name);
        if (new_root_provider == nullptr) {
          *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("Certificate provider instance name: \"",
                           root_provider_instance_name, "\" not recognized.")
                  .c_str());
          return false;
        }
      }
      // Configure identity cert.
      absl::string_view identity_provider_instance_name =
          listener.downstream_tls_context.common_tls_context
              .tls_certificate_certificate_provider_instance.instance_name;
      absl::string_view identity_provider_cert_name =
          listener.downstream_tls_context.common_tls_context
              .tls_certificate_certificate_provider_instance.certificate_name;
      RefCountedPtr<grpc_tls_certificate_provider> new_identity_provider;
      if (!identity_provider_instance_name.empty()) {
        new_identity_provider = xds_client_->certificate_provider_store()
                                    .CreateOrGetCertificateProvider(
                                        identity_provider_instance_name);
        if (new_identity_provider == nullptr) {
          *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("Certificate provider instance name: \"",
                           identity_provider_instance_name,
                           "\" not recognized.")
                  .c_str());
          return false;
        }
      }
      bool security_connector_update_required = false;
      if (((new_root_provider == nullptr) !=
           (root_certificate_provider_ == nullptr)) ||
          ((new_identity_provider == nullptr) !=
           (identity_certificate_provider_ == nullptr)) ||
          (listener.downstream_tls_context.require_client_certificate !=
           xds_certificate_provider_->GetRequireClientCertificate(""))) {
        security_connector_update_required = true;
      }
      if (root_certificate_provider_ != new_root_provider) {
        root_certificate_provider_ = std::move(new_root_provider);
      }
      if (identity_certificate_provider_ != new_identity_provider) {
        identity_certificate_provider_ = std::move(new_identity_provider);
      }
      xds_certificate_provider_->UpdateRootCertNameAndDistributor(
          "", root_provider_cert_name,
          root_certificate_provider_ == nullptr
              ? nullptr
              : root_certificate_provider_->distributor());
      xds_certificate_provider_->UpdateIdentityCertNameAndDistributor(
          "", identity_provider_cert_name,
          identity_certificate_provider_ == nullptr
              ? nullptr
              : identity_certificate_provider_->distributor());
      xds_certificate_provider_->UpdateRequireClientCertificate(
          "", listener.downstream_tls_context.require_client_certificate);
      return security_connector_update_required;
    }

    std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
        server_config_watcher_;
    grpc_channel_args* args_;
    RefCountedPtr<XdsClient> xds_client_;
    RefCountedPtr<grpc_tls_certificate_provider> root_certificate_provider_;
    RefCountedPtr<grpc_tls_certificate_provider> identity_certificate_provider_;
    RefCountedPtr<XdsCertificateProvider> xds_certificate_provider_;
    bool updated_once_ = false;
  };

  struct WatcherState {
    std::string listening_address;
    ListenerWatcher* listener_watcher = nullptr;
  };

  RefCountedPtr<XdsClient> xds_client_;
  Mutex mu_;
  std::map<grpc_server_config_fetcher::WatcherInterface*, WatcherState>
      watchers_;
};

}  // namespace
}  // namespace grpc_core

grpc_server_config_fetcher* grpc_server_config_fetcher_xds_create() {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_server_config_fetcher_xds_create()", 0, ());
  grpc_error* error = GRPC_ERROR_NONE;
  grpc_core::RefCountedPtr<grpc_core::XdsClient> xds_client =
      grpc_core::XdsClient::GetOrCreate(&error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "Failed to create xds client: %s",
            grpc_error_string(error));
    GRPC_ERROR_UNREF(error);
    return nullptr;
  }
  return new grpc_core::XdsServerConfigFetcher(std::move(xds_client));
}
