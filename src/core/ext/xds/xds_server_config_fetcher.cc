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

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/ext/xds/xds_client.h"

namespace grpc_core {

class XdsServerConfigFetcher : public Server::ConfigFetcherInterface {
 public:
  XdsServerConfigFetcher(Server* server, grpc_error** error)
      : server_(server),
        work_serializer_(new WorkSerializer()),
        interested_parties_(grpc_pollset_set_create()),
        xds_client_(new XdsClient(work_serializer_, interested_parties_,
                                  *server->channel_args(), error)) {}

  ~XdsServerConfigFetcher() {
    if (started_) {
      for (grpc_pollset* pollset : server_->pollsets()) {
        grpc_pollset_set_del_pollset(interested_parties_, pollset);
      }
    }
    grpc_pollset_set_destroy(interested_parties_);
  }

  void StartWatch(const grpc_channel_args& args,
                  std::vector<std::string> listening_addresses,
                  std::unique_ptr<WatcherInterface> watcher) override {
    // TODO(roth): We currently tell the server which listeners we want
    // via the listening_address field in the node message, which is
    // sent only at the start of the xDS stream.  Therefore, we cannot
    // handle new watchers being added after the creation of the xDS
    // stream, which means that we can't share the XdsClient between
    // gRPC servers.  When we move to the new xDS naming scheme, this
    // problem will go away, and it will be possible to share the xDS
    // client between gRPC servers.  As part of that, remove this assertion.
    GPR_ASSERT(!started_);
    auto& watcher_state = watchers_[watcher.get()];
    watcher_state.watcher = std::move(watcher);
    watcher_state.listening_addresses = std::move(listening_addresses);
    watcher_state.args = grpc_channel_args_copy(args);
  }

  void CancelWatch(WatcherInterface* watcher) override {
    watchers_.erase(watcher);
  }

  void Start() override {
    std::vector<std::string> listening_addresses;
    for (const auto& p : watchers_) {
      listening_addresses.insert(listening_addresses.end(),
                                 p.second.listening_addresses.begin(),
                                 p.second.listening_addresses.end());
    }
    work_serializer_->Run([this, listening_addresses]() {
      // TODO(roth): Remove this assertion when we move to the new xDS
      // naming scheme.
      GPR_ASSERT(listening_addresses.size() == 1);
      xds_client_->SetListenerWatcher(
          /*server_name=*/"", std::move(listening_addresses),
          absl::make_unique<ListenerWatcher>(Ref()));
      started_ = true;
    });
    // Set up polling.
    for (grpc_pollset* pollset : server_->pollsets()) {
      grpc_pollset_set_add_pollset(interested_parties_, pollset);
    }
// FIXME: start query, poll CQ, etc
  }

 private:
  struct WatcherState {
    std::unique_ptr<WatcherInterface> watcher;
    std::vector<std::string> listening_addresses;
    grpc_channel_args* args;
  };

  class ListenerWatcher : public XdsClient::ListenerWatcherInterface {
   public:
    explicit XdsServerConfigFetcher(
        RefCountedPtr<XdsServerConfigFetcher> config_fetcher)
        : config_fetcher_(std::move(config_fetcher)) {}

    void OnListenerChanged(XdsApi::LdsUpdate listener_data) override {
      for (const auto& p : config_fetcher_->watchers_) {
// FIXME: construct channel args based on listener response
        p.second.watcher->UpdateConfig(args);
      }
    }

    void OnError(grpc_error* error) override {
      gpr_log(GPR_ERROR, "XdsClient reports error: %s",
              grpc_error_string(error));
      GRPC_ERROR_UNREF(error);
    }

    void OnResourceDoesNotExist() override {
      gpr_log(GPR_ERROR, "XdsClient reports requested listener does not exist");
    }

   private:
    RefCountedPtr<XdsServerConfigFetcher> config_fetcher_;
  };

  Server* server_;
  bool started_ = false;
  std::shared_ptr<WorkSerializer> work_serializer_;
  grpc_pollset_set* interested_parties_;
  OrphanablePtr<XdsClient> xds_client_;

  Mutex mu_;
  std::map<WatcherInterface*, WatcherState> watchers_;
};

}  // namespace grpc_core

grpc_server_config_fetcher* grpc_server_config_fetcher_xds_create(
    grpc_server* server) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_server_config_fetcher_xds_create()", 0, ());
  grpc_error* error = GRPC_ERROR_NONE;
  auto* config_fetcher = absl::make_unique<grpc_server_config_fetcher>();
  config_fetcher->impl =
      grpc_core::MakeRefCounted<grpc_core::XdsServerConfigFetcher>(
          server->core_server.get(), &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "could not create xds config fetcher: %s",
            grpc_error_string(error));
    GRPC_ERROR_UNREF(error);
    return nullptr;
  }
  return config_fetcher.release();
}
