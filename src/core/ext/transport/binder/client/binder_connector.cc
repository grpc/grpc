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
#include <sys/un.h>
#endif

#include <functional>
#include <map>

#include <grpcpp/security/binder_security_policy.h>

#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
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
      GPR_ASSERT(id_length >= 2);
      // Make sure there is null terminator at the expected location before
      // reading from it
      GPR_ASSERT(un->sun_path[id_length - 1] == '\0');
      conn_id_ = un->sun_path;
    }
#else
    GPR_ASSERT(0);
#endif
    gpr_log(GPR_INFO, "BinderConnector %p conn_id_ = %s", this,
            conn_id_.c_str());

    args_ = args;
    GPR_ASSERT(notify_ == nullptr);
    GPR_ASSERT(notify != nullptr);
    notify_ = notify;
    result_ = result;

    Ref().release();  // Ref held by the following callback

    grpc_binder::GetEndpointBinderPool()->GetEndpointBinder(
        conn_id_,
        std::bind(&BinderConnector::OnConnected, this, std::placeholders::_1));
  }

  void OnConnected(std::unique_ptr<grpc_binder::Binder> endpoint_binder) {
    GPR_ASSERT(endpoint_binder != nullptr);
    grpc_transport* transport = grpc_create_binder_transport_client(
        std::move(endpoint_binder),
        grpc_binder::GetSecurityPolicySetting()->Get(conn_id_));
    GPR_ASSERT(transport != nullptr);
    result_->channel_args = args_.channel_args;
    result_->transport = transport;

    GPR_ASSERT(notify_ != nullptr);
    // ExecCtx is required here for running grpc_closure because this callback
    // might be invoked from non-gRPC code
    if (grpc_core::ExecCtx::Get() == nullptr) {
      grpc_core::ExecCtx exec_ctx;
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, notify_, GRPC_ERROR_NONE);
    } else {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, notify_, GRPC_ERROR_NONE);
    }

    Unref();  // Was referenced in BinderConnector::Connect
  }
  void Shutdown(grpc_error_handle error) override { (void)error; }

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
  gpr_log(GPR_INFO, "BinderClientChannelFactory creating subchannel %p", this);
  return Subchannel::Create(
      MakeOrphanable<BinderConnector>(), address,
      args.Set(GRPC_ARG_DEFAULT_AUTHORITY, "binder.authority"));
}

}  // namespace grpc_core

#endif  // GRPC_NO_BINDER
