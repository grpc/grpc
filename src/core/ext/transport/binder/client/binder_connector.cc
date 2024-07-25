// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#ifndef GRPC_NO_BINDER

#include "src/core/ext/transport/binder/client/binder_connector.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_HAVE_UNIX_SOCKET
#ifdef GPR_WINDOWS
// clang-format off
#include <ws2def.h>
#include <afunix.h>
// clang-format on
#else
#include <sys/un.h>
#endif  // GPR_WINDOWS
#endif

#include <functional>
#include <map>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include <grpcpp/security/binder_security_policy.h>

#include "src/core/client_channel/connector.h"
#include "src/core/client_channel/subchannel.h"
#include "src/core/ext/transport/binder/client/endpoint_binder_pool.h"
#include "src/core/ext/transport/binder/client/security_policy_setting.h"
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"

namespace {

// TODO(mingcl): Currently this does no error handling and assumes the
// connection always succeeds in reasonable amount of time.
class BinderConnector : public grpc_core::SubchannelConnector {
 public:
  BinderConnector() {}
  ~BinderConnector() override {}
  void Connect(const Args& args, Result* result,
               grpc_closure* notify) override {
#ifdef GRPC_HAVE_UNIX_SOCKET
    {
      struct sockaddr_un* un =
          reinterpret_cast<struct sockaddr_un*>(args.address->addr);
      // length of identifier, including null terminator
      size_t id_length = args.address->len - sizeof(un->sun_family);
      // The c-style string at least will have a null terminator, and the
      // connection id itself should not be empty
      CHECK_GE(id_length, 2u);
      // Make sure there is null terminator at the expected location before
      // reading from it
      CHECK_EQ(un->sun_path[id_length - 1], '\0');
      conn_id_ = un->sun_path;
    }
#else
    CHECK(0);
#endif
    LOG(INFO) << "BinderConnector " << this << " conn_id_ = " << conn_id_;

    args_ = args;
    CHECK_EQ(notify_, nullptr);
    CHECK_NE(notify, nullptr);
    notify_ = notify;
    result_ = result;

    Ref().release();  // Ref held by the following callback

    grpc_binder::GetEndpointBinderPool()->GetEndpointBinder(
        conn_id_,
        std::bind(&BinderConnector::OnConnected, this, std::placeholders::_1));
  }

  void OnConnected(std::unique_ptr<grpc_binder::Binder> endpoint_binder) {
    CHECK(endpoint_binder != nullptr);
    grpc_core::Transport* transport = grpc_create_binder_transport_client(
        std::move(endpoint_binder),
        grpc_binder::GetSecurityPolicySetting()->Get(conn_id_));
    CHECK_NE(transport, nullptr);
    result_->channel_args = args_.channel_args;
    result_->transport = transport;

    CHECK_NE(notify_, nullptr);
    // ExecCtx is required here for running grpc_closure because this callback
    // might be invoked from non-gRPC code
    if (grpc_core::ExecCtx::Get() == nullptr) {
      grpc_core::ExecCtx exec_ctx;
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, notify_, absl::OkStatus());
    } else {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, notify_, absl::OkStatus());
    }

    Unref();  // Was referenced in BinderConnector::Connect
  }
  void Shutdown(grpc_error_handle /*error*/) override {}

 private:
  Args args_;
  grpc_closure* notify_ = nullptr;
  Result* result_ = nullptr;

  std::string conn_id_;
};

}  // namespace

namespace grpc_core {

RefCountedPtr<Subchannel> BinderClientChannelFactory::CreateSubchannel(
    const grpc_resolved_address& address, const ChannelArgs& args) {
  LOG(INFO) << "BinderClientChannelFactory creating subchannel " << this;
  return Subchannel::Create(
      MakeOrphanable<BinderConnector>(), address,
      args.Set(GRPC_ARG_DEFAULT_AUTHORITY, "binder.authority"));
}

}  // namespace grpc_core

#endif  // GRPC_NO_BINDER
