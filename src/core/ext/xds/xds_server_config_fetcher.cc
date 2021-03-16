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

#include "absl/strings/str_replace.h"

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
  explicit XdsServerConfigFetcher(RefCountedPtr<XdsClient> xds_client,
                                  grpc_server_xds_status_notifier notifier)
      : xds_client_(std::move(xds_client)), serving_status_notifier_(notifier) {
    GPR_ASSERT(xds_client_ != nullptr);
  }

  void StartWatch(std::string listening_address, grpc_channel_args* args,
                  std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
                      watcher) override {
    grpc_server_config_fetcher::WatcherInterface* watcher_ptr = watcher.get();
    auto listener_watcher = absl::make_unique<ListenerWatcher>(
        std::move(watcher), args, xds_client_, serving_status_notifier_,
        listening_address);
    auto* listener_watcher_ptr = listener_watcher.get();
    listening_address = absl::StrReplaceAll(
        xds_client_->bootstrap().server_listener_resource_name_template(),
        {{"%s", listening_address}});
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
        grpc_channel_args* args, RefCountedPtr<XdsClient> xds_client,
        grpc_server_xds_status_notifier serving_status_notifier,
        std::string listening_address)
        : server_config_watcher_(std::move(server_config_watcher)),
          args_(args),
          xds_client_(std::move(xds_client)),
          serving_status_notifier_(serving_status_notifier),
          listening_address_(std::move(listening_address)) {}

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
      if (listener.address != listening_address_) {
        OnFatalError(absl::FailedPreconditionError(
            "Address in LDS update does not match listening address"));
        return;
      }
      grpc_error* error = GRPC_ERROR_NONE;
      bool update_needed = UpdateXdsCertificateProvider(listener, &error);
      if (error != GRPC_ERROR_NONE) {
        OnError(error);
        return;
      }
      // Only send an update, if something changed.
      if (have_resource_ && !update_needed) {
        return;
      }
      if (!have_resource_) {
        have_resource_ = true;
        if (serving_status_notifier_.on_serving_status_change != nullptr) {
          serving_status_notifier_.on_serving_status_change(
              serving_status_notifier_.user_data, listening_address_.c_str(),
              GRPC_STATUS_OK, "");
        } else {
          gpr_log(GPR_INFO,
                  "xDS Listener resource obtained; will start serving on %s",
                  listening_address_.c_str());
        }
      }
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
      if (have_resource_) {
        gpr_log(GPR_ERROR,
                "ListenerWatcher:%p XdsClient reports error: %s for %s; "
                "ignoring in favor of existing resource",
                this, grpc_error_string(error), listening_address_.c_str());
      } else {
        if (serving_status_notifier_.on_serving_status_change != nullptr) {
          serving_status_notifier_.on_serving_status_change(
              serving_status_notifier_.user_data, listening_address_.c_str(),
              GRPC_STATUS_UNAVAILABLE, grpc_error_string(error));
        } else {
          gpr_log(
              GPR_ERROR,
              "ListenerWatcher:%p error obtaining xDS Listener resource: %s; "
              "not serving on %s",
              this, grpc_error_string(error), listening_address_.c_str());
        }
      }
      GRPC_ERROR_UNREF(error);
    }

    void OnFatalError(absl::Status status) {
      gpr_log(
          GPR_ERROR,
          "ListenerWatcher:%p Encountered fatal error %s; not serving on %s",
          this, status.ToString().c_str(), listening_address_.c_str());
      if (have_resource_) {
        // The server has started listening already, so we need to gracefully
        // stop serving.
        server_config_watcher_->StopServing();
        have_resource_ = false;
      }
      if (serving_status_notifier_.on_serving_status_change != nullptr) {
        serving_status_notifier_.on_serving_status_change(
            serving_status_notifier_.user_data, listening_address_.c_str(),
            static_cast<grpc_status_code>(status.raw_code()),
            std::string(status.message()).c_str());
      }
    }

    void OnResourceDoesNotExist() override {
      OnFatalError(absl::NotFoundError("Requested listener does not exist"));
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
          listener.filter_chains[0]
              .downstream_tls_context.common_tls_context
              .combined_validation_context
              .validation_context_certificate_provider_instance.instance_name;
      absl::string_view root_provider_cert_name =
          listener.filter_chains[0]
              .downstream_tls_context.common_tls_context
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
          listener.filter_chains[0]
              .downstream_tls_context.common_tls_context
              .tls_certificate_certificate_provider_instance.instance_name;
      absl::string_view identity_provider_cert_name =
          listener.filter_chains[0]
              .downstream_tls_context.common_tls_context
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
          (listener.filter_chains[0]
               .downstream_tls_context.require_client_certificate !=
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
          "", listener.filter_chains[0]
                  .downstream_tls_context.require_client_certificate);
      return security_connector_update_required;
    }

    std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
        server_config_watcher_;
    grpc_channel_args* args_;
    RefCountedPtr<XdsClient> xds_client_;
    grpc_server_xds_status_notifier serving_status_notifier_;
    std::string listening_address_;
    RefCountedPtr<grpc_tls_certificate_provider> root_certificate_provider_;
    RefCountedPtr<grpc_tls_certificate_provider> identity_certificate_provider_;
    RefCountedPtr<XdsCertificateProvider> xds_certificate_provider_;
    bool have_resource_ = false;
  };

  struct WatcherState {
    std::string listening_address;
    ListenerWatcher* listener_watcher = nullptr;
  };

  RefCountedPtr<XdsClient> xds_client_;
  grpc_server_xds_status_notifier serving_status_notifier_;
  Mutex mu_;
  std::map<grpc_server_config_fetcher::WatcherInterface*, WatcherState>
      watchers_;
};

}  // namespace
}  // namespace grpc_core

grpc_server_config_fetcher* grpc_server_config_fetcher_xds_create(
    grpc_server_xds_status_notifier notifier) {
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
  if (xds_client->bootstrap()
          .server_listener_resource_name_template()
          .empty()) {
    gpr_log(GPR_ERROR,
            "server_listener_resource_name_template not provided in bootstrap "
            "file.");
    return nullptr;
  }
  return new grpc_core::XdsServerConfigFetcher(std::move(xds_client), notifier);
}
