//
//
// Copyright 2015-2016 gRPC authors.
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

#include <limits.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/compression_types.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <grpc/support/workaround_list.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/impl/server_builder_option.h>
#include <grpcpp/impl/server_builder_plugin.h>
#include <grpcpp/impl/service_type.h>
#include <grpcpp/resource_quota.h>
#include <grpcpp/security/authorization_policy_provider.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_interface.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/server_interceptor.h>

#include "src/core/ext/transport/chttp2/server/chttp2_server.h"
#include "src/core/server/server.h"
#include "src/core/util/string.h"
#include "src/core/util/useful.h"
#include "src/cpp/server/external_connection_acceptor_impl.h"

namespace grpc {
namespace {

// A PIMPL wrapper class that owns the only ref to the passive listener
// implementation. This is returned to the application.
class PassiveListenerOwner final
    : public grpc_core::experimental::PassiveListener {
 public:
  explicit PassiveListenerOwner(std::shared_ptr<PassiveListener> listener)
      : listener_(std::move(listener)) {}

  absl::Status AcceptConnectedEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint) override {
    return listener_->AcceptConnectedEndpoint(std::move(endpoint));
  }

  absl::Status AcceptConnectedFd(int fd) override {
    return listener_->AcceptConnectedFd(fd);
  }

 private:
  std::shared_ptr<PassiveListener> listener_;
};

}  // namespace

static std::vector<std::unique_ptr<ServerBuilderPlugin> (*)()>*
    g_plugin_factory_list;
static gpr_once once_init_plugin_list = GPR_ONCE_INIT;

static void do_plugin_list_init(void) {
  g_plugin_factory_list =
      new std::vector<std::unique_ptr<ServerBuilderPlugin> (*)()>();
}

ServerBuilder::ServerBuilder()
    : max_receive_message_size_(INT_MIN),
      max_send_message_size_(INT_MIN),
      sync_server_settings_(SyncServerSettings()),
      resource_quota_(nullptr) {
  gpr_once_init(&once_init_plugin_list, do_plugin_list_init);
  for (const auto& value : *g_plugin_factory_list) {
    plugins_.emplace_back(value());
  }

  // all compression algorithms enabled by default.
  enabled_compression_algorithms_bitset_ =
      (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1;
  memset(&maybe_default_compression_level_, 0,
         sizeof(maybe_default_compression_level_));
  memset(&maybe_default_compression_algorithm_, 0,
         sizeof(maybe_default_compression_algorithm_));
}

ServerBuilder::~ServerBuilder() {
  if (resource_quota_ != nullptr) {
    grpc_resource_quota_unref(resource_quota_);
  }
}

std::unique_ptr<grpc::ServerCompletionQueue> ServerBuilder::AddCompletionQueue(
    bool is_frequently_polled) {
  grpc::ServerCompletionQueue* cq = new grpc::ServerCompletionQueue(
      GRPC_CQ_NEXT,
      is_frequently_polled ? GRPC_CQ_DEFAULT_POLLING : GRPC_CQ_NON_LISTENING,
      nullptr);
  cqs_.push_back(cq);
  return std::unique_ptr<grpc::ServerCompletionQueue>(cq);
}

ServerBuilder& ServerBuilder::RegisterService(Service* service) {
  services_.emplace_back(new NamedService(service));
  return *this;
}

ServerBuilder& ServerBuilder::RegisterService(const std::string& host,
                                              Service* service) {
  services_.emplace_back(new NamedService(host, service));
  return *this;
}

ServerBuilder& ServerBuilder::RegisterAsyncGenericService(
    AsyncGenericService* service) {
  if (generic_service_ || callback_generic_service_) {
    LOG(ERROR) << "Adding multiple generic services is unsupported for now. "
                  "Dropping the service "
               << service;
  } else {
    generic_service_ = service;
  }
  return *this;
}

ServerBuilder& ServerBuilder::RegisterCallbackGenericService(
    CallbackGenericService* service) {
  if (generic_service_ || callback_generic_service_) {
    LOG(ERROR) << "Adding multiple generic services is unsupported for now. "
                  "Dropping the service "
               << service;
  } else {
    callback_generic_service_ = service;
  }
  return *this;
}

ServerBuilder& ServerBuilder::SetContextAllocator(
    std::unique_ptr<grpc::ContextAllocator> context_allocator) {
  context_allocator_ = std::move(context_allocator);
  return *this;
}

std::unique_ptr<grpc::experimental::ExternalConnectionAcceptor>
ServerBuilder::experimental_type::AddExternalConnectionAcceptor(
    experimental_type::ExternalConnectionType type,
    std::shared_ptr<ServerCredentials> creds) {
  std::string name_prefix("external:");
  char count_str[GPR_LTOA_MIN_BUFSIZE];
  gpr_ltoa(static_cast<long>(builder_->acceptors_.size()), count_str);
  builder_->acceptors_.emplace_back(
      std::make_shared<grpc::internal::ExternalConnectionAcceptorImpl>(
          name_prefix.append(count_str), type, creds));
  return builder_->acceptors_.back()->GetAcceptor();
}

void ServerBuilder::experimental_type::SetAuthorizationPolicyProvider(
    std::shared_ptr<experimental::AuthorizationPolicyProviderInterface>
        provider) {
  builder_->authorization_provider_ = std::move(provider);
}

void ServerBuilder::experimental_type::EnableCallMetricRecording(
    experimental::ServerMetricRecorder* server_metric_recorder) {
  builder_->AddChannelArgument(GRPC_ARG_SERVER_CALL_METRIC_RECORDING, 1);
  CHECK_EQ(builder_->server_metric_recorder_, nullptr);
  builder_->server_metric_recorder_ = server_metric_recorder;
}

ServerBuilder& ServerBuilder::SetOption(
    std::unique_ptr<ServerBuilderOption> option) {
  options_.push_back(std::move(option));
  return *this;
}

ServerBuilder& ServerBuilder::SetSyncServerOption(
    ServerBuilder::SyncServerOption option, int val) {
  switch (option) {
    case NUM_CQS:
      sync_server_settings_.num_cqs = val;
      break;
    case MIN_POLLERS:
      sync_server_settings_.min_pollers = val;
      break;
    case MAX_POLLERS:
      sync_server_settings_.max_pollers = val;
      break;
    case CQ_TIMEOUT_MSEC:
      sync_server_settings_.cq_timeout_msec = val;
      break;
  }
  return *this;
}

ServerBuilder& ServerBuilder::SetCompressionAlgorithmSupportStatus(
    grpc_compression_algorithm algorithm, bool enabled) {
  if (enabled) {
    grpc_core::SetBit(&enabled_compression_algorithms_bitset_, algorithm);
  } else {
    grpc_core::ClearBit(&enabled_compression_algorithms_bitset_, algorithm);
  }
  return *this;
}

ServerBuilder& ServerBuilder::SetDefaultCompressionLevel(
    grpc_compression_level level) {
  maybe_default_compression_level_.is_set = true;
  maybe_default_compression_level_.level = level;
  return *this;
}

ServerBuilder& ServerBuilder::SetDefaultCompressionAlgorithm(
    grpc_compression_algorithm algorithm) {
  maybe_default_compression_algorithm_.is_set = true;
  maybe_default_compression_algorithm_.algorithm = algorithm;
  return *this;
}

ServerBuilder& ServerBuilder::SetResourceQuota(
    const grpc::ResourceQuota& resource_quota) {
  if (resource_quota_ != nullptr) {
    grpc_resource_quota_unref(resource_quota_);
  }
  resource_quota_ = resource_quota.c_resource_quota();
  grpc_resource_quota_ref(resource_quota_);
  return *this;
}

ServerBuilder& ServerBuilder::experimental_type::AddPassiveListener(
    std::shared_ptr<grpc::ServerCredentials> creds,
    std::unique_ptr<experimental::PassiveListener>& passive_listener) {
  auto core_passive_listener =
      std::make_shared<grpc_core::experimental::PassiveListenerImpl>();
  builder_->unstarted_passive_listeners_.emplace_back(core_passive_listener,
                                                      std::move(creds));
  passive_listener =
      std::make_unique<PassiveListenerOwner>(std::move(core_passive_listener));
  return *builder_;
}

ServerBuilder& ServerBuilder::AddListeningPort(
    const std::string& addr_uri, std::shared_ptr<ServerCredentials> creds,
    int* selected_port) {
  const std::string uri_scheme = "dns:";
  std::string addr = addr_uri;
  if (addr_uri.compare(0, uri_scheme.size(), uri_scheme) == 0) {
    size_t pos = uri_scheme.size();
    while (addr_uri[pos] == '/') ++pos;  // Skip slashes.
    addr = addr_uri.substr(pos);
  }
  Port port = {addr, std::move(creds), selected_port};
  ports_.push_back(port);
  return *this;
}

ChannelArguments ServerBuilder::BuildChannelArgs() {
  ChannelArguments args;
  if (max_receive_message_size_ >= -1) {
    args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, max_receive_message_size_);
  }
  if (max_send_message_size_ >= -1) {
    args.SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, max_send_message_size_);
  }
  for (const auto& option : options_) {
    option->UpdateArguments(&args);
    option->UpdatePlugins(&plugins_);
  }
  args.SetInt(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET,
              enabled_compression_algorithms_bitset_);
  if (maybe_default_compression_level_.is_set) {
    args.SetInt(GRPC_COMPRESSION_CHANNEL_DEFAULT_LEVEL,
                maybe_default_compression_level_.level);
  }
  if (maybe_default_compression_algorithm_.is_set) {
    args.SetInt(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM,
                maybe_default_compression_algorithm_.algorithm);
  }
  if (resource_quota_ != nullptr) {
    args.SetPointerWithVtable(GRPC_ARG_RESOURCE_QUOTA, resource_quota_,
                              grpc_resource_quota_arg_vtable());
  }
  for (const auto& plugin : plugins_) {
    plugin->UpdateServerBuilder(this);
    plugin->UpdateChannelArguments(&args);
  }
  if (authorization_provider_ != nullptr) {
    args.SetPointerWithVtable(GRPC_ARG_AUTHORIZATION_POLICY_PROVIDER,
                              authorization_provider_->c_provider(),
                              grpc_authorization_policy_provider_arg_vtable());
  }
  return args;
}

std::unique_ptr<grpc::Server> ServerBuilder::BuildAndStart() {
  ChannelArguments args = BuildChannelArgs();

  // == Determine if the server has any syncrhonous methods ==
  bool has_sync_methods = false;
  for (const auto& value : services_) {
    if (value->service->has_synchronous_methods()) {
      has_sync_methods = true;
      break;
    }
  }

  if (!has_sync_methods) {
    for (const auto& value : plugins_) {
      if (value->has_sync_methods()) {
        has_sync_methods = true;
        break;
      }
    }
  }

  // If this is a Sync server, i.e a server expositing sync API, then the server
  // needs to create some completion queues to listen for incoming requests.
  // 'sync_server_cqs' are those internal completion queues.
  //
  // This is different from the completion queues added to the server via
  // ServerBuilder's AddCompletionQueue() method (those completion queues
  // are in 'cqs_' member variable of ServerBuilder object)
  std::shared_ptr<std::vector<std::unique_ptr<grpc::ServerCompletionQueue>>>
      sync_server_cqs(
          std::make_shared<
              std::vector<std::unique_ptr<grpc::ServerCompletionQueue>>>());

  bool has_frequently_polled_cqs = false;
  for (const auto& cq : cqs_) {
    if (cq->IsFrequentlyPolled()) {
      has_frequently_polled_cqs = true;
      break;
    }
  }

  // == Determine if the server has any callback methods ==
  bool has_callback_methods = false;
  for (const auto& service : services_) {
    if (service->service->has_callback_methods()) {
      has_callback_methods = true;
      has_frequently_polled_cqs = true;
      break;
    }
  }

  if (callback_generic_service_ != nullptr) {
    has_frequently_polled_cqs = true;
  }

  const bool is_hybrid_server = has_sync_methods && has_frequently_polled_cqs;

  if (has_sync_methods) {
    grpc_cq_polling_type polling_type =
        is_hybrid_server ? GRPC_CQ_NON_POLLING : GRPC_CQ_DEFAULT_POLLING;

    // Create completion queues to listen to incoming rpc requests
    for (int i = 0; i < sync_server_settings_.num_cqs; i++) {
      sync_server_cqs->emplace_back(
          new grpc::ServerCompletionQueue(GRPC_CQ_NEXT, polling_type, nullptr));
    }
  }

  // TODO(vjpai): Add a section here for plugins once they can support callback
  // methods

  if (has_sync_methods) {
    // This is a Sync server
    VLOG(2) << "Synchronous server. Num CQs: " << sync_server_settings_.num_cqs
            << ", Min pollers: " << sync_server_settings_.min_pollers
            << ", Max Pollers: " << sync_server_settings_.max_pollers
            << ", CQ timeout (msec): " << sync_server_settings_.cq_timeout_msec;
  }

  if (has_callback_methods) {
    VLOG(2) << "Callback server.";
  }

  std::unique_ptr<grpc::Server> server(new grpc::Server(
      &args, sync_server_cqs, sync_server_settings_.min_pollers,
      sync_server_settings_.max_pollers, sync_server_settings_.cq_timeout_msec,
      std::move(acceptors_), server_config_fetcher_, resource_quota_,
      std::move(interceptor_creators_), server_metric_recorder_));

  ServerInitializer* initializer = server->initializer();

  // Register all the completion queues with the server. i.e
  //  1. sync_server_cqs: internal completion queues created IF this is a sync
  //     server
  //  2. cqs_: Completion queues added via AddCompletionQueue() call

  for (const auto& cq : *sync_server_cqs) {
    grpc_server_register_completion_queue(server->server_, cq->cq(), nullptr);
    has_frequently_polled_cqs = true;
  }

  if (has_callback_methods || callback_generic_service_ != nullptr) {
    auto* cq = server->CallbackCQ();
    grpc_server_register_completion_queue(server->server_, cq->cq(), nullptr);
  }

  // cqs_ contains the completion queue added by calling the ServerBuilder's
  // AddCompletionQueue() API. Some of them may not be frequently polled (i.e by
  // calling Next() or AsyncNext()) and hence are not safe to be used for
  // listening to incoming channels. Such completion queues must be registered
  // as non-listening queues. In debug mode, these should have their server list
  // tracked since these are provided the user and must be Shutdown by the user
  // after the server is shutdown.
  for (const auto& cq : cqs_) {
    grpc_server_register_completion_queue(server->server_, cq->cq(), nullptr);
    cq->RegisterServer(server.get());
  }

  for (auto& unstarted_listener : unstarted_passive_listeners_) {
    has_frequently_polled_cqs = true;
    auto passive_listener = unstarted_listener.passive_listener.lock();
    auto* core_server = grpc_core::Server::FromC(server->c_server());
    if (passive_listener != nullptr) {
      auto* creds = unstarted_listener.credentials->c_creds();
      if (creds == nullptr) {
        LOG(ERROR) << "Credentials missing for PassiveListener";
        return nullptr;
      }
      auto success = grpc_server_add_passive_listener(
          core_server, creds, std::move(passive_listener));
      if (!success.ok()) {
        LOG(ERROR) << "Failed to create a passive listener: "
                   << success.ToString();
        return nullptr;
      }
    }
  }

  if (!has_frequently_polled_cqs) {
    LOG(ERROR)
        << "At least one of the completion queues must be frequently polled";
    return nullptr;
  }

  server->RegisterContextAllocator(std::move(context_allocator_));

  for (const auto& value : services_) {
    if (!server->RegisterService(value->host.get(), value->service)) {
      return nullptr;
    }
  }

  for (const auto& value : plugins_) {
    value->InitServer(initializer);
  }

  if (generic_service_) {
    server->RegisterAsyncGenericService(generic_service_);
  } else if (callback_generic_service_) {
    server->RegisterCallbackGenericService(callback_generic_service_);
  } else {
    for (const auto& value : services_) {
      if (value->service->has_generic_methods()) {
        LOG(ERROR) << "Some methods were marked generic but there is no "
                      "generic service registered.";
        return nullptr;
      }
    }
  }

  bool added_port = false;
  for (auto& port : ports_) {
    int r = server->AddListeningPort(port.addr, port.creds.get());
    if (!r) {
      if (added_port) server->Shutdown();
      return nullptr;
    }
    added_port = true;
    if (port.selected_port != nullptr) {
      *port.selected_port = r;
    }
  }

  auto cqs_data = cqs_.empty() ? nullptr : &cqs_[0];
  server->Start(cqs_data, cqs_.size());

  for (const auto& value : plugins_) {
    value->Finish(initializer);
  }

  return server;
}

void ServerBuilder::InternalAddPluginFactory(
    std::unique_ptr<ServerBuilderPlugin> (*CreatePlugin)()) {
  gpr_once_init(&once_init_plugin_list, do_plugin_list_init);
  (*g_plugin_factory_list).push_back(CreatePlugin);
}

ServerBuilder& ServerBuilder::EnableWorkaround(grpc_workaround_list id) {
  switch (id) {
    case GRPC_WORKAROUND_ID_CRONET_COMPRESSION:
      return AddChannelArgument(GRPC_ARG_WORKAROUND_CRONET_COMPRESSION, 1);
    default:
      LOG(ERROR) << "Workaround " << id << " does not exist or is obsolete.";
      return *this;
  }
}

}  // namespace grpc
