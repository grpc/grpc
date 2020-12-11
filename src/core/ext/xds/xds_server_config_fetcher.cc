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

#include "src/core/ext/xds/xds_client.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

namespace grpc_core {
namespace {

class XdsServerConfigFetcher : public grpc_server_config_fetcher {
 public:
  XdsServerConfigFetcher() {
    grpc_error* error = GRPC_ERROR_NONE;
    xds_client_ = XdsClient::GetOrCreate(&error);
    if (error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Failed to create xds client: %s",
              grpc_error_string(error));
    }
  }

  void StartWatch(std::string listening_address,
                  std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
                      watcher) override {
    auto listener_watcher = absl::make_unique<ListenerWatcher>(watcher.get());
    auto* listener_watcher_ptr = listener_watcher.get();
    xds_client_->WatchListenerData(listening_address,
                                   std::move(listener_watcher));
    MutexLock lock(&mu_);
    auto& watcher_state = watchers_[watcher.get()];
    watcher_state.listening_address = listening_address;
    watcher_state.watcher = std::move(watcher);
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
    }
    watchers_.erase(watcher);
  }

  // Return the interested parties from the xds client so that it can be polled.
  grpc_pollset_set* interested_parties() override {
    return xds_client_->interested_parties();
  }

 private:
  class ListenerWatcher : public XdsClient::ListenerWatcherInterface {
   public:
    explicit ListenerWatcher(
        grpc_server_config_fetcher::WatcherInterface* server_config_watcher)
        : server_config_watcher_(server_config_watcher) {}

    void OnListenerChanged(XdsApi::LdsUpdate listener) override {
      // TODO(yashykt): Construct channel args according to received update
      server_config_watcher_->UpdateConfig(nullptr);
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
    grpc_server_config_fetcher::WatcherInterface* server_config_watcher_;
  };

  struct WatcherState {
    std::string listening_address;
    std::unique_ptr<grpc_server_config_fetcher::WatcherInterface> watcher;
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
  return new grpc_core::XdsServerConfigFetcher;
}
