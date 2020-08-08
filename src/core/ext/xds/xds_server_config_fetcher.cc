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

class XdsServerConfigFetcher : public grpc_server_config_fetcher {
 public:
  XdsServerConfigFetcher(Server* server, grpc_error** error)
      : server_(server),
        work_serializer_(new WorkSerializer()),
        interested_parties_(grpc_pollset_set_create()),
        xds_client_(new XdsClient(work_serializer_, interested_parties_,
                                  // XXX, XXX,
                                  *server->channel_args(), error)) {}

  ~XdsServerConfigFetcher() {
    if (started_) {
      for (grpc_pollset* pollset : server_->pollsets()) {
        grpc_pollset_set_del_pollset(interested_parties_, pollset);
      }
    }
    grpc_pollset_set_destroy(interested_parties_);
  }

  void Start(const std::vector<std::string>& listening_addresses,
             grpc_channel_args* args) {
    for (grpc_pollset* pollset : server_->pollsets()) {
      grpc_pollset_set_add_pollset(interested_parties_, pollset);
    }
// FIXME: start query, poll CQ, etc
// call server_->set_channel_args() to update
    started_ = true;
  }

 private:
  Server* server_;
  bool started_ = false;
  std::shared_ptr<WorkSerializer> work_serializer_;
  grpc_pollset_set* interested_parties_;
  OrphanablePtr<XdsClient> xds_client_;
};

}  // namespace grpc_core

grpc_server_config_fetcher* grpc_server_config_fetcher_xds_create(
    grpc_server* server) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_server_config_fetcher_xds_create()", 0, ());
  grpc_error* error = GRPC_ERROR_NONE;
  auto config_fetcher = absl::make_unique<grpc_core::XdsServerConfigFetcher>(
      server->core_server.get(), &error);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "could not create xds config fetcher: %s",
            grpc_error_string(error));
    GRPC_ERROR_UNREF(error);
    return nullptr;
  }
  return config_fetcher.release();
}
