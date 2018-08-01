/*
 *
 * Copyright 2015-2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpcpp/server_builder.h>

#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/service_type.h>
#include <grpcpp/resource_quota.h>
#include <grpcpp/server.h>

#include <utility>

#include "src/core/lib/gpr/useful.h"
#include "src/cpp/server/thread_pool_interface.h"

namespace grpc {

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
      resource_quota_(nullptr),
      generic_service_(nullptr) {
  gpr_once_init(&once_init_plugin_list, do_plugin_list_init);
  for (auto it = g_plugin_factory_list->begin();
       it != g_plugin_factory_list->end(); it++) {
    auto& factory = *it;
    plugins_.emplace_back(factory());
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

std::unique_ptr<ServerCompletionQueue> ServerBuilder::AddCompletionQueue(
    bool is_frequently_polled) {
  ServerCompletionQueue* cq = new ServerCompletionQueue(
      is_frequently_polled ? GRPC_CQ_DEFAULT_POLLING : GRPC_CQ_NON_LISTENING);
  cqs_.push_back(cq);
  return std::unique_ptr<ServerCompletionQueue>(cq);
}

ServerBuilder& ServerBuilder::RegisterService(Service* service) {
  services_.emplace_back(new NamedService(service));
  return *this;
}

ServerBuilder& ServerBuilder::RegisterService(const grpc::string& addr,
                                              Service* service) {
  services_.emplace_back(new NamedService(addr, service));
  return *this;
}

ServerBuilder& ServerBuilder::RegisterAsyncGenericService(
    AsyncGenericService* service) {
  if (generic_service_) {
    gpr_log(GPR_ERROR,
            "Adding multiple AsyncGenericService is unsupported for now. "
            "Dropping the service %p",
            (void*)service);
  } else {
    generic_service_ = service;
  }
  return *this;
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
    GPR_BITSET(&enabled_compression_algorithms_bitset_, algorithm);
  } else {
    GPR_BITCLEAR(&enabled_compression_algorithms_bitset_, algorithm);
  }
  return *this;
}

ServerBuilder& ServerBuilder::SetDefaultCompressionLevel(
    grpc_compression_level level) {
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

ServerBuilder& ServerBuilder::AddListeningPort(
    const grpc::string& addr_uri, std::shared_ptr<ServerCredentials> creds,
    int* selected_port) {
  const grpc::string uri_scheme = "dns:";
  grpc::string addr = addr_uri;
  if (addr_uri.compare(0, uri_scheme.size(), uri_scheme) == 0) {
    size_t pos = uri_scheme.size();
    while (addr_uri[pos] == '/') ++pos;  // Skip slashes.
    addr = addr_uri.substr(pos);
  }
  Port port = {addr, std::move(creds), selected_port};
  ports_.push_back(port);
  return *this;
}

std::unique_ptr<Server> ServerBuilder::BuildAndStart() {
  ChannelArguments args;
  for (auto option = options_.begin(); option != options_.end(); ++option) {
    (*option)->UpdateArguments(&args);
    (*option)->UpdatePlugins(&plugins_);
  }

  for (auto plugin = plugins_.begin(); plugin != plugins_.end(); plugin++) {
    (*plugin)->UpdateServerBuilder(this);
    (*plugin)->UpdateChannelArguments(&args);
  }

  if (max_receive_message_size_ >= -1) {
    args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, max_receive_message_size_);
  }

  // The default message size is -1 (max), so no need to explicitly set it for
  // -1.
  if (max_send_message_size_ >= 0) {
    args.SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, max_send_message_size_);
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

  // == Determine if the server has any syncrhonous methods ==
  bool has_sync_methods = false;
  for (auto it = services_.begin(); it != services_.end(); ++it) {
    if ((*it)->service->has_synchronous_methods()) {
      has_sync_methods = true;
      break;
    }
  }

  if (!has_sync_methods) {
    for (auto plugin = plugins_.begin(); plugin != plugins_.end(); plugin++) {
      if ((*plugin)->has_sync_methods()) {
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
  std::shared_ptr<std::vector<std::unique_ptr<ServerCompletionQueue>>>
      sync_server_cqs(std::make_shared<
                      std::vector<std::unique_ptr<ServerCompletionQueue>>>());

  int num_frequently_polled_cqs = 0;
  for (auto it = cqs_.begin(); it != cqs_.end(); ++it) {
    if ((*it)->IsFrequentlyPolled()) {
      num_frequently_polled_cqs++;
    }
  }

  const bool is_hybrid_server =
      has_sync_methods && num_frequently_polled_cqs > 0;

  if (has_sync_methods) {
    grpc_cq_polling_type polling_type =
        is_hybrid_server ? GRPC_CQ_NON_POLLING : GRPC_CQ_DEFAULT_POLLING;

    // Create completion queues to listen to incoming rpc requests
    for (int i = 0; i < sync_server_settings_.num_cqs; i++) {
      sync_server_cqs->emplace_back(new ServerCompletionQueue(polling_type));
    }
  }

  std::unique_ptr<Server> server(new Server(
      max_receive_message_size_, &args, sync_server_cqs,
      sync_server_settings_.min_pollers, sync_server_settings_.max_pollers,
      sync_server_settings_.cq_timeout_msec));

  if (has_sync_methods) {
    // This is a Sync server
    gpr_log(GPR_INFO,
            "Synchronous server. Num CQs: %d, Min pollers: %d, Max Pollers: "
            "%d, CQ timeout (msec): %d",
            sync_server_settings_.num_cqs, sync_server_settings_.min_pollers,
            sync_server_settings_.max_pollers,
            sync_server_settings_.cq_timeout_msec);
  }

  ServerInitializer* initializer = server->initializer();

  // Register all the completion queues with the server. i.e
  //  1. sync_server_cqs: internal completion queues created IF this is a sync
  //     server
  //  2. cqs_: Completion queues added via AddCompletionQueue() call

  for (auto it = sync_server_cqs->begin(); it != sync_server_cqs->end(); ++it) {
    grpc_server_register_completion_queue(server->server_, (*it)->cq(),
                                          nullptr);
    num_frequently_polled_cqs++;
  }

  // cqs_ contains the completion queue added by calling the ServerBuilder's
  // AddCompletionQueue() API. Some of them may not be frequently polled (i.e by
  // calling Next() or AsyncNext()) and hence are not safe to be used for
  // listening to incoming channels. Such completion queues must be registered
  // as non-listening queues
  for (auto it = cqs_.begin(); it != cqs_.end(); ++it) {
    grpc_server_register_completion_queue(server->server_, (*it)->cq(),
                                          nullptr);
  }

  if (num_frequently_polled_cqs == 0) {
    gpr_log(GPR_ERROR,
            "At least one of the completion queues must be frequently polled");
    return nullptr;
  }

  for (auto service = services_.begin(); service != services_.end();
       service++) {
    if (!server->RegisterService((*service)->host.get(), (*service)->service)) {
      return nullptr;
    }
  }

  for (auto plugin = plugins_.begin(); plugin != plugins_.end(); plugin++) {
    (*plugin)->InitServer(initializer);
  }

  if (generic_service_) {
    server->RegisterAsyncGenericService(generic_service_);
  } else {
    for (auto it = services_.begin(); it != services_.end(); ++it) {
      if ((*it)->service->has_generic_methods()) {
        gpr_log(GPR_ERROR,
                "Some methods were marked generic but there is no "
                "generic service registered.");
        return nullptr;
      }
    }
  }

  bool added_port = false;
  for (auto port = ports_.begin(); port != ports_.end(); port++) {
    int r = server->AddListeningPort(port->addr, port->creds.get());
    if (!r) {
      if (added_port) server->Shutdown();
      return nullptr;
    }
    added_port = true;
    if (port->selected_port != nullptr) {
      *port->selected_port = r;
    }
  }

  auto cqs_data = cqs_.empty() ? nullptr : &cqs_[0];
  server->Start(cqs_data, cqs_.size());

  for (auto plugin = plugins_.begin(); plugin != plugins_.end(); plugin++) {
    (*plugin)->Finish(initializer);
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
      gpr_log(GPR_ERROR, "Workaround %u does not exist or is obsolete.", id);
      return *this;
  }
}

}  // namespace grpc
