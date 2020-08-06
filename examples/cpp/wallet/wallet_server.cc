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
#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "examples/protos/account.grpc.pb.h"
#include "examples/protos/stats.grpc.pb.h"
#include "examples/protos/wallet.grpc.pb.h"

using account::Account;
using account::GetUserInfoRequest;
using account::GetUserInfoResponse;
using account::MembershipType;
using grpc::Channel;
using grpc::ChannelArguments;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using grpc::StatusCode;
using stats::PriceRequest;
using stats::PriceResponse;
using stats::Stats;
using wallet::BalanceRequest;
using wallet::BalanceResponse;
using wallet::Wallet;

class WalletServiceImpl final : public Wallet::Service {
 public:
  WalletServiceImpl(const std::string& hostname, const bool v1_behavior)
      : hostname_(hostname), v1_behavior_(v1_behavior) {}

  void SetStatsClientStub(std::unique_ptr<Stats::Stub> stub) {
    stats_stub_ = std::move(stub);
  }

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

  int ObtainAndBuildPerAddressResponse(const int price,
                                       const BalanceRequest* request,
                                       BalanceResponse* response) {
    int total_balance = 0;
    for (auto& address_entry : user_coin_map_[user_]) {
      int per_address_balance = address_entry.second * price;
      total_balance += per_address_balance;
      if (!v1_behavior_ && request->include_balance_per_address()) {
        auto* address = response->add_addresses();
        address->set_address(address_entry.first);
        address->set_balance(per_address_balance);
      }
    }
    return total_balance;
  }

  Status FetchBalance(ServerContext* context, const BalanceRequest* request,
                      BalanceResponse* response) override {
    if (!ObtainAndValidateUserAndMembership(context)) {
      return Status(StatusCode::UNAUTHENTICATED,
                    "membership authentication failed");
    }
    context->AddInitialMetadata("hostname", hostname_);
    ClientContext stats_context;
    stats_context.AddMetadata("authorization", token_);
    stats_context.AddMetadata("membership", membership_);
    PriceRequest stats_request;
    PriceResponse stats_response;
    // Call Stats Server to fetch the price to calculate the balance.
    Status stats_status =
        stats_stub_->FetchPrice(&stats_context, stats_request, &stats_response);
    if (stats_status.ok()) {
      auto metadata_hostname =
          stats_context.GetServerInitialMetadata().find("hostname");
      if (metadata_hostname != stats_context.GetServerInitialMetadata().end()) {
        std::cout << "server host: "
                  << std::string(metadata_hostname->second.data(),
                                 metadata_hostname->second.length())
                  << std::endl;
      }
      std::cout << "grpc-coin price " << stats_response.price() << std::endl;
    } else {
      std::cout << stats_status.error_code() << ": "
                << stats_status.error_message() << std::endl;
    }
    int total_balance = ObtainAndBuildPerAddressResponse(stats_response.price(),
                                                         request, response);
    response->set_balance(total_balance);
    return Status::OK;
  }

  Status WatchBalance(ServerContext* context, const BalanceRequest* request,
                      ServerWriter<BalanceResponse>* writer) override {
    if (!ObtainAndValidateUserAndMembership(context)) {
      return Status(StatusCode::UNAUTHENTICATED,
                    "membership authentication failed");
    }
    context->AddInitialMetadata("hostname", hostname_);
    ClientContext stats_context;
    stats_context.AddMetadata("authorization", token_);
    stats_context.AddMetadata("membership", membership_);
    PriceRequest stats_request;
    PriceResponse stats_response;
    // Open a streaming price watching with Stats Server.
    // Every time a response
    // is received, use the price to calculate the balance.
    // Send every updated balance in a stream back to the client.
    std::unique_ptr<ClientReader<PriceResponse>> stats_reader(
        stats_stub_->WatchPrice(&stats_context, stats_request));
    bool first_read = true;
    while (stats_reader->Read(&stats_response)) {
      if (first_read) {
        auto metadata_hostname =
            stats_context.GetServerInitialMetadata().find("hostname");
        if (metadata_hostname !=
            stats_context.GetServerInitialMetadata().end()) {
          std::cout << "server host: "
                    << std::string(metadata_hostname->second.data(),
                                   metadata_hostname->second.length())
                    << std::endl;
        }
        first_read = false;
      }
      std::cout << "grpc-coin price: " << stats_response.price() << std::endl;
      BalanceResponse response;
      int total_balance = ObtainAndBuildPerAddressResponse(
          stats_response.price(), request, &response);
      response.set_balance(total_balance);
      if (!writer->Write(response)) {
        break;
      }
    }
    return Status::OK;
  }

  std::string hostname_;
  bool v1_behavior_ = false;
  std::unique_ptr<Stats::Stub> stats_stub_;
  std::unique_ptr<Account::Stub> account_stub_;
  std::string token_;
  std::string user_ = "Alice";
  std::string membership_ = "premium";
  std::map<std::string, std::map<std::string, int>> user_coin_map_ = {
      {"Alice", {{"cd0aa985", 314}, {"454349e4", 159}}},
      {"Bob", {{"148de9c5", 271}, {"2e7d2c03", 828}}}};
};

void RunServer(const std::string& port, const std::string& account_server,
               const std::string& stats_server,
               const std::string& hostname_suffix, const bool v1_behavior) {
  char base_hostname[256];
  if (gethostname(base_hostname, 256) != 0) {
    sprintf(base_hostname, "%s-%d", "generated", rand() % 1000);
  }
  std::string hostname(base_hostname);
  hostname += hostname_suffix;
  std::string server_address("0.0.0.0:");
  server_address += port;
  WalletServiceImpl service(hostname, v1_behavior);
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  // Instantiate the client stubs.  It requires a channel, out of which the
  // actual RPCs are created.  The channel models a connection to an endpoint
  // (Stats Server and Account Server in this case).  We indicate that the
  // channel isn't authenticated (use of InsecureChannelCredentials()).
  ChannelArguments args;
  service.SetStatsClientStub(Stats::NewStub(grpc::CreateCustomChannel(
      stats_server, grpc::InsecureChannelCredentials(), args)));
  service.SetAccountClientStub(Account::NewStub(grpc::CreateCustomChannel(
      account_server, grpc::InsecureChannelCredentials(), args)));
  // Listen on the given address without any authentication mechanism.
  std::cout << "Wallet server listening on " << server_address << std::endl;
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
  std::string port = "50051";
  std::string account_server = "localhost:50053";
  std::string stats_server = "localhost:50052";
  std::string hostname_suffix = "";
  bool v1_behavior = false;
  std::string arg_str_port("--port");
  std::string arg_str_account_server("--account_server");
  std::string arg_str_stats_server("--stats_server");
  std::string arg_str_hostname_suffix("--hostname_suffix");
  std::string arg_str_v1_behavior("--v1_behavior");
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
    start_pos = arg_val.find(arg_str_stats_server);
    if (start_pos != std::string::npos) {
      start_pos += arg_str_stats_server.size();
      if (arg_val[start_pos] == '=') {
        stats_server = arg_val.substr(start_pos + 1);
        continue;
      } else {
        std::cout << "The only correct argument syntax is --stats_server="
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
    start_pos = arg_val.find(arg_str_v1_behavior);
    if (start_pos != std::string::npos) {
      start_pos += arg_str_v1_behavior.size();
      if (arg_val[start_pos] == '=') {
        if (arg_val.substr(start_pos + 1) == "true") {
          v1_behavior = true;
          continue;
        } else if (arg_val.substr(start_pos + 1) == "false") {
          v1_behavior = false;
          continue;
        } else {
          std::cout << "The only correct value for argument --v1_behavior is "
                       "true or false"
                    << std::endl;
          return 1;
        }
      } else {
        std::cout << "The only correct argument syntax is --v1_behavior="
                  << std::endl;
        return 1;
      }
    }
  }
  std::cout << "Wallet Server arguments: port: " << port
            << ", account_server: " << account_server
            << ", stats_server: " << stats_server
            << ", hostname_suffix: " << hostname_suffix
            << ", v1_behavior: " << v1_behavior << std::endl;
  RunServer(port, account_server, stats_server, hostname_suffix, v1_behavior);
  return 0;
}
