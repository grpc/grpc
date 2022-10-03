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

#include "test/core/transport/binder/end2end/testing_channel_create.h"

#include <utility>

#include <grpcpp/security/binder_security_policy.h>

#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader_impl.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_binder {
namespace end2end_testing {

namespace {
// Since we assume the first half of the transport setup is completed before the
// server side enters WireReader::SetupTransport, we need this helper to wait
// and finish that part of the negotiation for us.
class ServerSetupTransportHelper {
 public:
  ServerSetupTransportHelper()
      : wire_reader_(std::make_unique<WireReaderImpl>(
            /*transport_stream_receiver=*/nullptr, /*is_client=*/false,
            std::make_shared<
                grpc::experimental::binder::UntrustedSecurityPolicy>())) {
    std::tie(endpoint_binder_, tx_receiver_) = NewBinderPair(
        [this](transaction_code_t tx_code, ReadableParcel* parcel, int uid) {
          return this->wire_reader_->ProcessTransaction(tx_code, parcel, uid);
        });
  }
  std::unique_ptr<Binder> WaitForClientBinder() {
    return wire_reader_->RecvSetupTransport();
  }

  std::unique_ptr<Binder> GetEndpointBinderForClient() {
    return std::move(endpoint_binder_);
  }

 private:
  std::unique_ptr<WireReaderImpl> wire_reader_;
  // The endpoint binder for client.
  std::unique_ptr<Binder> endpoint_binder_;
  std::unique_ptr<TransactionReceiver> tx_receiver_;
};
}  // namespace

std::pair<grpc_transport*, grpc_transport*>
CreateClientServerBindersPairForTesting() {
  ServerSetupTransportHelper helper;
  std::unique_ptr<Binder> endpoint_binder = helper.GetEndpointBinderForClient();
  grpc_transport* client_transport = nullptr;

  struct ThreadArgs {
    std::unique_ptr<Binder> endpoint_binder;
    grpc_transport** client_transport;
  } args;

  args.endpoint_binder = std::move(endpoint_binder);
  args.client_transport = &client_transport;

  grpc_core::Thread client_thread(
      "client-thread",
      [](void* arg) {
        ThreadArgs* args = static_cast<ThreadArgs*>(arg);
        std::unique_ptr<Binder> endpoint_binder =
            std::move(args->endpoint_binder);
        *args->client_transport = grpc_create_binder_transport_client(
            std::move(endpoint_binder),
            std::make_shared<
                grpc::experimental::binder::UntrustedSecurityPolicy>());
      },
      &args);
  client_thread.Start();
  grpc_transport* server_transport = grpc_create_binder_transport_server(
      helper.WaitForClientBinder(),
      std::make_shared<grpc::experimental::binder::UntrustedSecurityPolicy>());
  client_thread.Join();
  return std::make_pair(client_transport, server_transport);
}

std::shared_ptr<grpc::Channel> BinderChannelForTesting(
    grpc::Server* server, const grpc::ChannelArguments& args) {
  grpc_channel_args channel_args = args.c_channel_args();
  return grpc::CreateChannelInternal(
      "",
      grpc_binder_channel_create_for_testing(server->c_server(), &channel_args,
                                             nullptr),
      std::vector<std::unique_ptr<
          grpc::experimental::ClientInterceptorFactoryInterface>>());
}

}  // namespace end2end_testing
}  // namespace grpc_binder

grpc_channel* grpc_binder_channel_create_for_testing(
    grpc_server* server, const grpc_channel_args* args, void* /*reserved*/) {
  grpc_core::ExecCtx exec_ctx;

  auto server_args = grpc_core::CoreConfiguration::Get()
                         .channel_args_preconditioning()
                         .PreconditionChannelArgs(args);
  auto client_args =
      server_args.Set(GRPC_ARG_DEFAULT_AUTHORITY, "test.authority");

  grpc_transport *client_transport, *server_transport;
  std::tie(client_transport, server_transport) =
      grpc_binder::end2end_testing::CreateClientServerBindersPairForTesting();
  grpc_error_handle error = grpc_core::Server::FromC(server)->SetupTransport(
      server_transport, nullptr, server_args, nullptr);
  GPR_ASSERT(error.ok());
  auto channel = grpc_core::Channel::Create(
      "binder", client_args, GRPC_CLIENT_DIRECT_CHANNEL, client_transport);
  GPR_ASSERT(channel.ok());
  return channel->release()->c_ptr();
}
