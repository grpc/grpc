/*
 *
 * Copyright 2015, Google Inc.
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

#include "test/cpp/qps/driver.h"
#include "src/core/support/env.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/host_port.h>
#include <grpc++/channel_arguments.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/stream.h>
#include <list>
#include <thread>
#include <vector>

using std::list;
using std::thread;
using std::unique_ptr;
using std::vector;
using grpc::string;
using grpc::ChannelArguments;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::CreateChannelDeprecated;
using grpc::Status;
using grpc::testing::ClientArgs;
using grpc::testing::ClientConfig;
using grpc::testing::ClientResult;
using grpc::testing::Worker;
using grpc::testing::ServerArgs;
using grpc::testing::ServerConfig;
using grpc::testing::ServerStatus;

#if 0
static vector<string> get_hosts(const string& name) {
  char* env = gpr_getenv(name.c_str());
  if (!env) return vector<string>();

  vector<string> out;
  char* p = env;
  for (;;) {
  	char* comma = strchr(p, ',');
  	if (comma) {
  	  out.emplace_back(p, comma);
  	  p = comma + 1;
  	} else {
  	  out.emplace_back(p);
  	  gpr_free(env);
  	  return out;
  	}
  }
}

void RunScenario(const ClientConfig& client_config, size_t num_clients,
                 const ServerConfig& server_config, size_t num_servers) {
  // ClientContext allocator (all are destroyed at scope exit)
  list<ClientContext> contexts;
  auto alloc_context = [&contexts]() {
  	contexts.emplace_back();
  	return &contexts.back();
  };

  // Get client, server lists
  auto workers = get_hosts("QPS_WORKERS");

  GPR_ASSERT(clients.size() >= num_clients);
  GPR_ASSERT(servers.size() >= num_servers);

  // Trim to just what we need
  clients.resize(num_clients);
  servers.resize(num_servers);

  // Start servers
  vector<unique_ptr<QpsServer::Stub>> server_stubs;
  vector<unique_ptr<ClientReaderWriter<ServerArgs, ServerStatus>>> server_streams;
  vector<string> server_targets;
  for (const auto& target : servers) {
  	server_stubs.push_back(QpsServer::NewStub(CreateChannelDeprecated(target, ChannelArguments())));
  	auto* stub = server_stubs.back().get();
  	ServerArgs args;
  	*args.mutable_config() = server_config;
  	server_streams.push_back(stub->RunServer(alloc_context()));
  	auto* stream = server_streams.back().get();
  	if (!stream->Write(args)) {
  	  gpr_log(GPR_ERROR, "Failed starting server");
  	  return;
  	}
  	ServerStatus init_status;
  	if (!stream->Read(&init_status)) {
  	  gpr_log(GPR_ERROR, "Failed starting server");
  	  return;
  	}
  	char* host;
  	char* driver_port;
  	char* cli_target;
  	gpr_split_host_port(target.c_str(), &host, &driver_port);
  	gpr_join_host_port(&cli_target, host, init_status.port());
  	server_targets.push_back(cli_target);
  	gpr_free(host);
  	gpr_free(driver_port);
  	gpr_free(cli_target);
  }

  // Start clients
  class Client {
   public:
   	Client(ClientContext* ctx, const string& target, const ClientArgs& args)
   	  : thread_([ctx, target, args, this]() {
   	  	auto stub = QpsClient::NewStub(CreateChannelDeprecated(target, ChannelArguments()));
   	  	status_ = stub->RunTest(ctx, args, &result_);
   	  }) {}

   	~Client() { join(); }

   	void join() { if (!joined_) { thread_.join(); joined_ = true; } }

   	const Status& status() const { return status_; }
   	const ClientResult& result() const { return result_; }

   private:
   	bool joined_ = false;
   	Status status_;
   	ClientResult result_;
   	thread thread_;
  };
  list<Client> running_clients;
  size_t svr_idx = 0;
  for (const auto& target : clients) {
  	ClientArgs args;
  	*args.mutable_config() = client_config;
  	for (size_t i = 0; i < num_servers; i++) {
  	  args.add_server_targets(server_targets[svr_idx]);
  	  svr_idx = (svr_idx + 1) % num_servers;
  	}

  	running_clients.emplace_back(alloc_context(), target, args);
  }

  // Finish clients
  for (auto& client : running_clients) {
  	client.join();
  	if (!client.status().IsOk()) {
  	  gpr_log(GPR_ERROR, "Client failed");
  	  return;
  	}
  }

  // Finish servers
  for (auto& stream : server_streams) {
  	ServerStatus final_status;
  	ServerStatus dummy;
  	if (!stream->WritesDone() || !stream->Read(&final_status) || stream->Read(&dummy) || !stream->Finish().IsOk()) {
  	  gpr_log(GPR_ERROR, "Server protocol error");
  	}
  }
}
#endif