//
// Copyright 2022 gRPC authors.
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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include <grpcpp/ext/admin_services.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/iomgr/gethostname.h"
#include "src/proto/grpc/testing/istio_echo.grpc.pb.h"
#include "src/proto/grpc/testing/istio_echo.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/test_config.h"

// A list of ports to listen on, for gRPC traffic.
ABSL_FLAG(std::vector<std::string>, grpc, std::vector<std::string>({"7070"}),
          "GRPC ports");

// The following flags must be defined, but are not used for now. Some may be
// necessary for certain tests.
ABSL_FLAG(std::vector<std::string>, port, std::vector<std::string>({"8080"}),
          "HTTP/1.1 ports");
ABSL_FLAG(std::vector<std::string>, tcp, std::vector<std::string>({"9090"}),
          "TCP ports");
ABSL_FLAG(std::vector<std::string>, tls, std::vector<std::string>({""}),
          "Ports that are using TLS. These must be defined as http/grpc/tcp.");
ABSL_FLAG(std::vector<std::string>, bind_ip, std::vector<std::string>({""}),
          "Ports that are bound to INSTANCE_IP rather than wildcard IP.");
ABSL_FLAG(std::vector<std::string>, bind_localhost,
          std::vector<std::string>({""}),
          "Ports that are bound to localhost rather than wildcard IP.");
ABSL_FLAG(std::vector<std::string>, server_first,
          std::vector<std::string>({""}),
          "Ports that are server first. These must be defined as tcp.");
ABSL_FLAG(std::vector<std::string>, xds_grpc_server,
          std::vector<std::string>({""}),
          "Ports that should rely on XDS configuration to serve");
ABSL_FLAG(std::string, metrics, "", "Metrics port");
ABSL_FLAG(std::string, uds, "", "HTTP server on unix domain socket");
ABSL_FLAG(std::string, cluster, "", "Cluster where this server is deployed");
ABSL_FLAG(std::string, crt, "", "gRPC TLS server-side certificate");
ABSL_FLAG(std::string, key, "", "gRPC TLS server-side key");
ABSL_FLAG(std::string, istio_version, "", "Istio sidecar version");
ABSL_FLAG(std::string, disable_alpn, "", "disable ALPN negotiation");

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using proto::EchoRequest;
using proto::EchoResponse;
using proto::EchoTestService;
using proto::ForwardEchoRequest;
using proto::ForwardEchoResponse;

const std::string host_key = "Host";
const std::string request_id_field = "X-Request-Id";
const std::string service_version_field = "ServiceVersion";
const std::string service_port_field = "ServicePort";
const std::string status_code_field = "StatusCode";
const std::string url_field = "URL";
const std::string host_field = "Host";
const std::string hostname_field = "Hostname";
const std::string method_field = "Method";
const std::string response_header = "ResponseHeader";
const std::string cluster_field = "Cluster";
const std::string istio_version_field = "IstioVersion";
const std::string ip_field = "IP";  // The Requester’s IP Address.

class EchoTestServiceImpl : public EchoTestService::Service {
 public:
  explicit EchoTestServiceImpl(const std::string& hostname)
      : hostname_(hostname) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    std::string s = "";
    const std::multimap<grpc::string_ref, grpc::string_ref> metadata =
        context->client_metadata();
    for (const auto& kv : metadata) {
      // Skip all binary headers.
      size_t isbin = kv.first.find("-bin");
      if ((isbin != std::string::npos) && (isbin + 4 == kv.first.size())) {
        continue;
      }
      if (kv.first == ":authority") {
        absl::StrAppend(&s, host_key, "=", kv.second.data(), "\n");
      } else {
        absl::StrAppend(&s, kv.first.data(), "=", kv.second.data(), "\n");
      }
    }
    std::string peer = context->peer();
    size_t colon = peer.find_first_of(':');
    std::string ip = peer.substr(0, colon);

    // This is not a complete list, but also not all fields are used. May
    //  need to add/remove fields later, if required by tests. Only keep the
    //  fields needed for now.
    //
    //  absl::StrAppend(&s,service_version_field,"=",this->version_,"\n");
    //  absl::StrAppend(&s,service_port_field,"=",this->port_,"\n");
    //  absl::StrAppend(&s,cluster_field,"=",this->cluster_,"\n");
    //  absl::StrAppend(&s,istio_version_field,"=",this->istio_version_,"\n");
    absl::StrAppend(&s, ip_field, "=", ip, "\n");
    absl::StrAppend(&s, status_code_field, "=", std::to_string(200), "\n");
    absl::StrAppend(&s, hostname_field, "=", this->hostname_, "\n");
    absl::StrAppend(&s, "Echo=", request->message(), "\n");
    response->set_message(s);
    return Status::OK;
  }

  Status ForwardEcho(ServerContext* /*context*/,
                     const ForwardEchoRequest* request,
                     ForwardEchoResponse* response) override {
    std::string rawUrl = request->url();
    size_t colon = rawUrl.find_first_of(':');
    std::string urlScheme = rawUrl.substr(0, colon);
    // May need to use xds security if urlScheme is "xds"
    std::string address = rawUrl;
    if (urlScheme == "grpc") {
      address = rawUrl.substr(strlen("grpc://"), std::string::npos);
    }
    std::shared_ptr<Channel> channel =
        grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    std::unique_ptr<EchoTestService::Stub> stub_ =
        EchoTestService::NewStub(channel);
    CompletionQueue cq_;

    auto count = request->count() == 0 ? 1 : request->count();
    std::vector<std::string> responses_(count);
    std::thread thread_ = std::thread(&EchoTestServiceImpl::AsyncCompleteRpc,
                                      this, &cq_, count, &responses_);
    std::chrono::duration<double> elapsed;
    std::chrono::duration<double> duration_per_query =
        std::chrono::nanoseconds::zero();
    if (request->qps() > 0) {
      duration_per_query =
          std::chrono::nanoseconds(std::chrono::seconds(1)) / request->qps();
    }
    std::chrono::time_point<std::chrono::system_clock> start =
        std::chrono::system_clock::now();
    for (int i = 0; i < count;) {
      elapsed = std::chrono::system_clock::now() - start;
      if (elapsed > duration_per_query) {
        start = std::chrono::system_clock::now();
        // Send the request.
        EchoCall* call = new EchoCall;
        std::chrono::system_clock::time_point deadline =
            std::chrono::system_clock::now() +
            std::chrono::microseconds(request->timeout_micros());
        call->context.set_deadline(deadline);
        for (const auto& data : request->headers()) {
          if (data.key() != host_key) {
            call->context.AddMetadata(data.key(), data.value());
          }
        }
        call->context.AddMetadata("x-request-id", std::to_string(i));
        EchoRequest echo_request;
        echo_request.set_message(request->message());
        call->r_id = i;
        call->request = echo_request;
        call->response_reader =
            stub_->PrepareAsyncEcho(&call->context, echo_request, &cq_);
        call->response_reader->StartCall();
        call->response_reader->Finish(&call->reply, &call->status,
                                      static_cast<void*>(call));
        ++i;
      }
    }
    thread_.join();
    for (const auto& r : responses_) {
      response->add_output(r);
    }
    return Status::OK;
  }

  void AsyncCompleteRpc(CompletionQueue* cq_, int count,
                        std::vector<std::string>* responses_) {
    void* got_tag;
    bool ok = false;
    for (int i = 0; i < count && cq_->Next(&got_tag, &ok);) {
      ++i;
      EchoCall* call = static_cast<EchoCall*>(got_tag);
      GPR_ASSERT(ok);
      std::string s;
      if (call->status.ok()) {
        absl::StrAppend(&s, "[", call->r_id, "] grpcecho.Echo(",
                        call->request.message(), ")\n");
        std::stringstream resp_ss(call->reply.message());
        std::string line;
        while (std::getline(resp_ss, line, '\n')) {
          absl::StrAppend(&s, "[", call->r_id, " body]\n");
        }
        responses_->at(call->r_id) = s;
      } else {
        gpr_log(GPR_DEBUG, "RPC failed %d: %s", call->status.error_code(),
                call->status.error_message().c_str());
      }
      delete call;
    }
  }

 private:
  struct EchoCall {
    int r_id;  // The index of this call (5 for the 5th request sent).
    EchoRequest request;
    EchoResponse reply;
    ClientContext context;
    Status status;
    std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader;
  };
  std::string hostname_;
  // The following fields are not set yet. But we may need them later.
  //  int port_;
  //  std::string version_;
  //  std::string cluster_;
  //  std::string istio_version_;
};

/*std::vector<std::unique_ptr<grpc::Server>>*/
void RunServer(std::vector<int> ports) {
  std::string hostname;
  char* hostname_p = grpc_gethostname();
  if (hostname_p == nullptr) {
    hostname = absl::StrFormat("generated-%d", rand() % 1000);
  } else {
    hostname.assign(hostname_p);
  }
  EchoTestServiceImpl echo_test_service(hostname);
  ServerBuilder builder;
  builder.RegisterService(&echo_test_service);
  for (int port : ports) {
    std::ostringstream server_address;
    server_address << "0.0.0.0:" << port;
    builder.AddListeningPort(server_address.str(),
                             grpc::InsecureServerCredentials());
    gpr_log(GPR_DEBUG, "Server listening on %s", server_address.str().c_str());
  }

  // 3333 is the magic port that the istio testing for k8s health checks. And
  // it only needs TCP. So also make the gRPC server to listen on 3333.
  std::ostringstream server_address_3333;
  server_address_3333 << "0.0.0.0:" << 3333;
  builder.AddListeningPort(server_address_3333.str(),
                           grpc::InsecureServerCredentials());
  std::unique_ptr<Server> server(builder.BuildAndStart());
  server->Wait();
}

int main(int argc, char** argv) {
  // Preprocess argv, for two things:
  // 1. merge duplciate flags. So "--grpc=8080 --grpc=9090" becomes
  // "--grpc=8080,9090".
  // 2. replace '-' to '_'. So "--istio-veriosn=123" becomes
  // "--istio_version=123".
  std::map<std::string, std::vector<std::string>> argv_dict;
  for (int i = 0; i < argc; i++) {
    std::string arg(argv[i]);
    size_t equal = arg.find_first_of('=');
    if (equal != std::string::npos) {
      std::string f = arg.substr(0, equal);
      std::string v = arg.substr(equal + 1, std::string::npos);
      argv_dict[f].push_back(v);
    }
  }

  std::vector<char*> new_argv_strs;
  // Keep the command itself.
  new_argv_strs.push_back(argv[0]);
  for (const auto& kv : argv_dict) {
    std::string values;
    for (const auto& s : kv.second) {
      if (!values.empty()) values += ",";
      values += s;
    }
    // replace '-' to '_', excluding the leading "--".
    std::string f = kv.first;
    std::replace(f.begin() + 2, f.end(), '-', '_');
    std::string k_vs = absl::StrCat(f, "=", values);
    char* writable = new char[k_vs.size() + 1];
    std::copy(k_vs.begin(), k_vs.end(), writable);
    writable[k_vs.size()] = '\0';
    new_argv_strs.push_back(writable);
  }
  int new_argc = new_argv_strs.size();
  char** new_argv = new_argv_strs.data();
  grpc::testing::TestEnvironment env(&new_argc, new_argv);
  grpc::testing::InitTest(&new_argc, &new_argv, true);
  // Turn gRPC ports from a string vector to an int vector.
  std::vector<int> grpc_ports;
  for (const auto& p : absl::GetFlag(FLAGS_grpc)) {
    int grpc_port = std::stoi(p);
    grpc_ports.push_back(grpc_port);
  }
  RunServer(grpc_ports);
  return 0;
}
