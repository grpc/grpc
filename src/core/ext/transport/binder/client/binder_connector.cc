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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/binder/client/binder_connector.h"

#include <functional>
#include <map>

#include "src/core/ext/transport/binder/client/endpoint_binder_pool.h"
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/filters/client_channel/connector.h"
#include "src/core/ext/filters/client_channel/subchannel.h"

namespace {

// TODO(mingcl): Currently this does not error handling and assumes the
// connection always successes in reasonable time. Also no thread safety is
// considered.
class BinderConnector : public grpc_core::SubchannelConnector {
 public:
  BinderConnector() {}
  ~BinderConnector() override {}
  void Connect(const Args& args, Result* result,
               grpc_closure* notify) override {
    std::string conn_id;
    {
      char tmp[sizeof(args.address->addr) + 1] = {0};
      strncpy(tmp, args.address->addr, sizeof(args.address->addr));
      conn_id = tmp;
    }
    gpr_log(GPR_ERROR, "conn_id = %s", conn_id.c_str());

    args_ = args;
    GPR_ASSERT(notify_ == nullptr);
    notify_ = notify;
    result_ = result;

    Ref().release();  // Ref held by the following callback

    grpc_binder::GetEndpointBinderPool()->GetEndpointBinder(
        conn_id,
        std::bind(&BinderConnector::OnConnected, this, std::placeholders::_1));
  }

  void OnConnected(std::unique_ptr<grpc_binder::Binder> endpoint_binder) {
    GPR_ASSERT(endpoint_binder != nullptr);
    grpc_transport* transport =
        grpc_create_binder_transport_client(std::move(endpoint_binder));
    GPR_ASSERT(transport != nullptr);
    result_->channel_args = grpc_channel_args_copy(args_.channel_args);
    result_->transport = transport;

    grpc_core::ExecCtx::Run(DEBUG_LOCATION, notify_, GRPC_ERROR_NONE);

    Unref();  // Was referenced in BinderConnector::Connect
  }
  void Shutdown(grpc_error_handle error) override { (void)error; }

 private:
  Args args_;
  grpc_closure* notify_ = nullptr;
  Result* result_ = nullptr;
};

}  // namespace

namespace grpc_core {

grpc_core::RefCountedPtr<grpc_core::Subchannel>
BinderClientChannelFactory::CreateSubchannel(
    const grpc_resolved_address& address, const grpc_channel_args* args) {
  gpr_log(GPR_ERROR, "BinderClientChannelFactory::CreateSubchannel called");
  grpc_arg default_authority_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
      const_cast<char*>("binder.authority"));
  grpc_channel_args* new_args =
      grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);

  grpc_core::RefCountedPtr<grpc_core::Subchannel> s =
      grpc_core::Subchannel::Create(
          grpc_core::MakeOrphanable<BinderConnector>(), address, new_args);

  return s;
}

}  // namespace grpc_core
