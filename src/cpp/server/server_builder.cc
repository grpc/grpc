/*
 *
 * Copyright 2015-2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc++/server_builder.h>

#include <grpc++/impl/service_type.h>
#include <grpc++/server.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include "src/cpp/server/thread_pool_interface.h"

namespace grpc {

ServerBuilder::ServerBuilder()
    : max_message_size_(-1), generic_service_(nullptr) {
  grpc_compression_options_init(&compression_options_);
}

std::unique_ptr<ServerCompletionQueue> ServerBuilder::AddCompletionQueue(
    bool is_frequently_polled) {
  ServerCompletionQueue* cq = new ServerCompletionQueue(is_frequently_polled);
  cqs_.push_back(cq);
  return std::unique_ptr<ServerCompletionQueue>(cq);
}

void ServerBuilder::RegisterService(Service* service) {
  services_.emplace_back(new NamedService(service));
}

void ServerBuilder::RegisterService(const grpc::string& addr,
                                    Service* service) {
  services_.emplace_back(new NamedService(addr, service));
}

void ServerBuilder::RegisterAsyncGenericService(AsyncGenericService* service) {
  if (generic_service_) {
    gpr_log(GPR_ERROR,
            "Adding multiple AsyncGenericService is unsupported for now. "
            "Dropping the service %p",
            service);
    return;
  }
  generic_service_ = service;
}

void ServerBuilder::SetOption(std::unique_ptr<ServerBuilderOption> option) {
  options_.push_back(std::move(option));
}

void ServerBuilder::AddListeningPort(const grpc::string& addr,
                                     std::shared_ptr<ServerCredentials> creds,
                                     int* selected_port) {
  Port port = {addr, creds, selected_port};
  ports_.push_back(port);
}

std::unique_ptr<Server> ServerBuilder::BuildAndStart() {
  std::unique_ptr<ThreadPoolInterface> thread_pool;
  // Does this server have atleast one sync method
  bool has_sync_methods = false;
  for (auto it = services_.begin(); it != services_.end(); ++it) {
    if ((*it)->service->has_synchronous_methods()) {
      has_sync_methods = true;
      if (thread_pool == nullptr) {
        thread_pool.reset(CreateDefaultThreadPool());
        break;
      }
    }
  }
  ChannelArguments args;
  for (auto option = options_.begin(); option != options_.end(); ++option) {
    (*option)->UpdateArguments(&args);
  }
  if (max_message_size_ > 0) {
    args.SetInt(GRPC_ARG_MAX_MESSAGE_LENGTH, max_message_size_);
  }
  args.SetInt(GRPC_COMPRESSION_ALGORITHM_STATE_ARG,
              compression_options_.enabled_algorithms_bitset);
  std::unique_ptr<Server> server(
      new Server(thread_pool.release(), true, max_message_size_, &args));

  // If the server has atleast one sync methods, we know that this is a Sync
  // server or a Hybrid server and the completion queue (server->cq_) would be
  // frequently polled.
  int num_frequently_polled_cqs = has_sync_methods ? 1 : 0;

  for (auto cq = cqs_.begin(); cq != cqs_.end(); ++cq) {
    // A completion queue that is not polled frequently (by calling Next() or
    // AsyncNext()) is not safe to use for listening to incoming channels.
    // Register all such completion queues as non-listening completion queues
    // with the GRPC core library.
    if ((*cq)->IsFrequentlyPolled()) {
      grpc_server_register_completion_queue(server->server_, (*cq)->cq(),
                                            nullptr);
      num_frequently_polled_cqs++;
    } else {
      grpc_server_register_non_listening_completion_queue(server->server_,
                                                          (*cq)->cq(), nullptr);
    }
  }

  if (num_frequently_polled_cqs == 0) {
    gpr_log(GPR_ERROR,
            "Atleast one of the completion queues must be frequently polled");
    return nullptr;
  }

  for (auto service = services_.begin(); service != services_.end();
       service++) {
    if (!server->RegisterService((*service)->host.get(), (*service)->service)) {
      return nullptr;
    }
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
  for (auto port = ports_.begin(); port != ports_.end(); port++) {
    int r = server->AddListeningPort(port->addr, port->creds.get());
    if (!r) return nullptr;
    if (port->selected_port != nullptr) {
      *port->selected_port = r;
    }
  }
  auto cqs_data = cqs_.empty() ? nullptr : &cqs_[0];
  if (!server->Start(cqs_data, cqs_.size())) {
    return nullptr;
  }
  return server;
}

}  // namespace grpc
