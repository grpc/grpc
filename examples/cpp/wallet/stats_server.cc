/*
 *
 * Copyright 2020 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <unistd.h>
#include <cmath>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "examples/protos/account.grpc.pb.h"
#include "examples/protos/stats.grpc.pb.h"

using account::Account;
using account::GetUserInfoRequest;
using account::GetUserInfoResponse;
using account::MembershipType;
using grpc::Channel;
using grpc::ChannelArguments;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using grpc::StatusCode;
using stats::PriceRequest;
using stats::PriceResponse;
using stats::Stats;

class StatsServiceImpl final : public Stats::Service {
 public:
  StatsServiceImpl(const std::string& hostname, const bool premium_only)
      : hostname_(hostname), premium_only_(premium_only) {}

  void SetAccountClientStub(std::unique_ptr<Account::Stub> stub) {
    account_stub_ = std::move(stub);
  }

 private:
  bool ObtainAndValidateUserAndMembership(ServerContext* server_context) {
    const std::multimap<grpc::string_ref, grpc::string_ref> metadata =
        server_context->client_metadata();
    for (auto iter = metadata.begin(); iter != metadata.end(); ++iter) {
      if (iter->first == "authorization")
        token_ = std::string(iter->second.data(), iter->second.size());
      else if (iter->first == "membership")
        membership_ = std::string(iter->second.data(), iter->second.size());
    }
    // Stats server started with --premium_only flag, but metadata says this is
    // from a non-premium request, reject it.
    if (premium_only_ && membership_ != "premium") {
      std::cout << "requested membership is non-premium but his this an "
                   "premium-only server"
                << std::endl;
      return false;
    }
    GetUserInfoRequest request;
    request.set_token(token_);
    ClientContext context;
    GetUserInfoResponse response;
    Status status = account_stub_->GetUserInfo(&context, request, &response);
    if (status.ok()) {
      auto metadata_hostname =
          context.GetServerInitialMetadata().find("hostname");
      if (metadata_hostname != context.GetServerInitialMetadata().end()) {
        std::cout << "server host: "
                  << std::string(metadata_hostname->second.data(),
                                 metadata_hostname->second.length())
                  << std::endl;
      }
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
    }
    user_ = response.name();
    // User requested premium service, but the user is not a premium user,
    // reject the request.
    if (membership_ == "premium" &&
        response.membership() != MembershipType::PREMIUM) {
      std::cout << "token: " << token_ << ", name: " << user_
                << ", membership: " << response.membership() << ","
                << std::endl;
      std::cout << "requested membership: " << membership_
                << ", authentication FAILED" << std::endl;
      return false;
    }
    std::cout << "token: " << token_ << ", name: " << user_
              << ", membership: " << response.membership() << "," << std::endl;
    std::cout << "requested membership: " << membership_
              << ", authentication success true" << std::endl;
    return true;
  }

  Status FetchPrice(ServerContext* context, const PriceRequest* request,
                    PriceResponse* response) override {
    if (!ObtainAndValidateUserAndMembership(context)) {
      return Status(StatusCode::UNAUTHENTICATED,
                    "membership authentication failed");
    }
    context->AddInitialMetadata("hostname", hostname_);
    response->set_price(sin(time(0) * 1000 / 173) * 1000 + 10000);
    return Status::OK;
  }

  Status WatchPrice(ServerContext* context, const PriceRequest* request,
                    ServerWriter<PriceResponse>* writer) override {
    if (!ObtainAndValidateUserAndMembership(context)) {
      return Status(StatusCode::UNAUTHENTICATED,
                    "membership authtication failed");
    }
    context->AddInitialMetadata("hostname", hostname_);
    while (true) {
      PriceResponse response;
      response.set_price(sin(time(0) * 1000 / 173) * 1000 + 10000);
      if (!writer->Write(response)) {
        break;
      }
      std::this_thread::sleep_for(
          std::chrono::milliseconds(membership_ == "premium" ? 100 : 1000));
    }
    return Status::OK;
  }

  std::string hostname_;
  bool premium_only_ = false;
  std::unique_ptr<Account::Stub> account_stub_;
  std::string token_;
  std::string user_ = "Alice";
  std::string membership_ = "premium";
};

void RunServer(const std::string& port, const std::string& account_server,
               const std::string& hostname_suffix, const bool premium_only) {
  char base_hostname[256];
  if (gethostname(base_hostname, 256) != 0) {
    sprintf(base_hostname, "%s-%d", "generated", rand() % 1000);
  }
  std::string hostname(base_hostname);
  hostname += hostname_suffix;
  std::string server_address("0.0.0.0:");
  server_address += port;
  StatsServiceImpl service(hostname, premium_only);
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  // Instantiate the client stub.  It requires a channel, out of which the
  // actual RPCs are created.  The channel models a connection to an endpoint
  // (Account Server in this case).  We indicate that the channel isn't
  // authenticated (use of InsecureChannelCredentials()).
  ChannelArguments args;
  service.SetAccountClientStub(Account::NewStub(grpc::CreateCustomChannel(
      account_server, grpc::InsecureChannelCredentials(), args)));
  // Listen on the given address without any authentication mechanism.
  std::cout << "Stats Server listening on " << server_address << std::endl;
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  std::string port = "50052";
  std::string account_server = "localhost:50053";
  std::string hostname_suffix = "";
  bool premium_only = false;
  std::string arg_str_port("--port");
  std::string arg_str_account_server("--account_server");
  std::string arg_str_hostname_suffix("--hostname_suffix");
  std::string arg_str_premium_only("--premium_only");
  for (int i = 1; i < argc; ++i) {
    std::string arg_val = argv[i];
    size_t start_pos = arg_val.find(arg_str_port);
    if (start_pos != std::string::npos) {
      start_pos += arg_str_port.size();
      if (arg_val[start_pos] == '=') {
        port = arg_val.substr(start_pos + 1);
        continue;
      } else {
        std::cout << "The only correct argument syntax is --port=" << std::endl;
        return 1;
      }
    }
    start_pos = arg_val.find(arg_str_account_server);
    if (start_pos != std::string::npos) {
      start_pos += arg_str_account_server.size();
      if (arg_val[start_pos] == '=') {
        account_server = arg_val.substr(start_pos + 1);
        continue;
      } else {
        std::cout << "The only correct argument syntax is --account_server="
                  << std::endl;
        return 1;
      }
    }
    start_pos = arg_val.find(arg_str_hostname_suffix);
    if (start_pos != std::string::npos) {
      start_pos += arg_str_hostname_suffix.size();
      if (arg_val[start_pos] == '=') {
        hostname_suffix = arg_val.substr(start_pos + 1);
        continue;
      } else {
        std::cout << "The only correct argument syntax is --hostname_suffix="
                  << std::endl;
        return 1;
      }
    }
    start_pos = arg_val.find(arg_str_premium_only);
    if (start_pos != std::string::npos) {
      start_pos += arg_str_premium_only.size();
      if (arg_val[start_pos] == '=') {
        if (arg_val.substr(start_pos + 1) == "true") {
          premium_only = true;
          continue;
        } else if (arg_val.substr(start_pos + 1) == "false") {
          premium_only = false;
          continue;
        } else {
          std::cout << "The only correct value for argument --premium_only is "
                       "true or false"
                    << std::endl;
          return 1;
        }
      } else {
        std::cout << "The only correct argument syntax is --premium_only="
                  << std::endl;
        return 1;
      }
    }
  }
  std::cout << "Stats Server arguments: port: " << port
            << ", account_server: " << account_server
            << ", hostname_suffix: " << hostname_suffix
            << ", premium_only: " << premium_only << std::endl;
  RunServer(port, account_server, hostname_suffix, premium_only);
  return 0;
}
