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

#ifndef GRPCPP_XDS_SERVER_BUILDER_H
#define GRPCPP_XDS_SERVER_BUILDER_H

#include <grpc/impl/codegen/port_platform.h>

#include <grpcpp/server_builder.h>

namespace grpc {
namespace experimental {

class XdsServerServingStatusNotifierInterface {
 public:
  virtual ~XdsServerServingStatusNotifierInterface() = default;

  // \a uri contains the listening target associated with the notification. Note
  // that a single target provided to XdsServerBuilder can get resolved to
  // multiple listening addresses.
  // The callback is invoked each time there is an update to the serving status.
  // The API does not provide any guarantees around duplicate updates.
  // Status::OK signifies that the server is serving, while a non-OK status
  // signifies that the server is not serving.
  virtual void OnServingStatusUpdate(std::string uri, grpc::Status status) = 0;
};

class XdsServerBuilder : public ::grpc::ServerBuilder {
 public:
  // It is the responsibility of the application to make sure that \a notifier
  // outlasts the life of the server. Notifications will start being made
  // asynchronously once `BuildAndStart()` has been called. Note that it is
  // possible for notifications to be made before `BuildAndStart()` returns.
  void set_status_notifier(XdsServerServingStatusNotifierInterface* notifier) {
    notifier_ = notifier;
  }

 private:
  // Called at the beginning of BuildAndStart().
  ChannelArguments BuildChannelArgs() override {
    ChannelArguments args = ServerBuilder::BuildChannelArgs();
    grpc_channel_args c_channel_args = args.c_channel_args();
    grpc_server_config_fetcher* fetcher = grpc_server_config_fetcher_xds_create(
        {OnServingStatusUpdate, notifier_}, &c_channel_args);
    if (fetcher != nullptr) set_fetcher(fetcher);
    return args;
  }

  static void OnServingStatusUpdate(void* user_data, const char* uri,
                                    grpc_status_code code,
                                    const char* error_message) {
    if (user_data == nullptr) return;
    XdsServerServingStatusNotifierInterface* notifier =
        static_cast<XdsServerServingStatusNotifierInterface*>(user_data);
    notifier->OnServingStatusUpdate(
        uri, grpc::Status(static_cast<StatusCode>(code), error_message));
  }

  XdsServerServingStatusNotifierInterface* notifier_ = nullptr;
};

}  // namespace experimental
}  // namespace grpc

#endif /* GRPCPP_XDS_SERVER_BUILDER_H */
