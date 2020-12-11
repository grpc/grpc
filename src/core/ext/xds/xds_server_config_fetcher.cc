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

#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/server.h"

namespace grpc_core {
namespace {

class XdsServerConfigFetcher : public grpc_server_config_fetcher {
 public:
  XdsServerConfigFetcher() : interested_parties_(grpc_pollset_set_create()) {}

  void StartWatch(std::string listening_address,
                  std::unique_ptr<grpc_server_config_fetcher::WatcherInterface>
                      watcher) override {}

  void CancelWatch(
      grpc_server_config_fetcher::WatcherInterface* watcher) override {}

  // Maybe return the interested parties from the xds client
  grpc_pollset_set* interested_parties() override {
    return interested_parties_;
  }

 private:
  grpc_pollset_set* interested_parties_;
};

}  // namespace
}  // namespace grpc_core

grpc_server_config_fetcher* grpc_server_config_fetcher_xds_create() {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_server_config_fetcher_xds_create()", 0, ());
  return new grpc_core::XdsServerConfigFetcher;
}
