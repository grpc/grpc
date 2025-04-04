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

#include <grpcpp/ext/admin_services.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/string_ref.h>
#include <grpcpp/xds_server_builder.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "src/core/call/status_util.h"
#include "src/core/util/env.h"
#include "src/core/util/gethostname.h"
#include "src/core/util/host_port.h"
#include "src/proto/grpc/testing/istio_echo.pb.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/interop/istio_echo_server_lib.h"
#include "test/cpp/util/test_config.h"

// A list of ports to listen on, for gRPC traffic.
ABSL_FLAG(std::vector<std::string>, grpc, std::vector<std::string>({"7070"}),
          "GRPC ports");
ABSL_FLAG(std::vector<std::string>, tls, std::vector<std::string>({}),
          "Ports that are using TLS. These must be defined as http/grpc/tcp.");
ABSL_FLAG(std::vector<std::string>, xds_grpc_server,
          std::vector<std::string>({}),
          "Ports that should rely on XDS configuration to serve");
ABSL_FLAG(std::string, crt, "", "gRPC TLS server-side certificate");
ABSL_FLAG(std::string, key, "", "gRPC TLS server-side key");
ABSL_FLAG(std::string, forwarding_address, "0.0.0.0:7072",
          "Forwarding address for unhandled protocols");
ABSL_FLAG(std::string, service_version, "", "Version string for the service");

// The following flags must be defined, but are not used for now. Some may be
// necessary for certain tests.
ABSL_FLAG(std::vector<std::string>, port, std::vector<std::string>({"8080"}),
          "HTTP/1.1 ports");
ABSL_FLAG(std::vector<std::string>, tcp, std::vector<std::string>({"9090"}),
          "TCP ports");
ABSL_FLAG(std::vector<std::string>, bind_ip, std::vector<std::string>({}),
          "Ports that are bound to INSTANCE_IP rather than wildcard IP.");
ABSL_FLAG(std::vector<std::string>, bind_localhost,
          std::vector<std::string>({}),
          "Ports that are bound to localhost rather than wildcard IP.");
ABSL_FLAG(std::vector<std::string>, server_first, std::vector<std::string>({}),
          "Ports that are server first. These must be defined as tcp.");
ABSL_FLAG(std::string, metrics, "", "Metrics port");
ABSL_FLAG(std::string, uds, "", "HTTP server on unix domain socket");
ABSL_FLAG(std::string, cluster, "", "Cluster where this server is deployed");
ABSL_FLAG(std::string, istio_version, "", "Istio sidecar version");
ABSL_FLAG(std::string, disable_alpn, "", "disable ALPN negotiation");

namespace grpc {
namespace testing {
namespace {

void RunServer(const std::set<int>& grpc_ports, const std::set<int>& xds_ports,
               const std::set<int>& tls_ports) {
  // Get hostname
  std::string hostname;
  char* hostname_p = grpc_gethostname();
  if (hostname_p == nullptr) {
    hostname = absl::StrFormat("generated-%d", rand() % 1000);
  } else {
    hostname = hostname_p;
    free(hostname_p);
  }
  EchoTestServiceImpl echo_test_service(
      std::move(hostname), absl::GetFlag(FLAGS_service_version),
      absl::GetFlag(FLAGS_forwarding_address));
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  XdsServerBuilder xds_builder;
  bool has_xds_listeners = false;
  builder.RegisterService(&echo_test_service);
  xds_builder.RegisterService(&echo_test_service);
  for (int port : grpc_ports) {
    auto server_address = grpc_core::JoinHostPort("0.0.0.0", port);
    if (xds_ports.find(port) != xds_ports.end()) {
      xds_builder.AddListeningPort(
          server_address, XdsServerCredentials(InsecureServerCredentials()));
      LOG(INFO) << "Server listening on " << server_address << " over xds";
      has_xds_listeners = true;
    } else if (tls_ports.find(port) != tls_ports.end()) {
      // Create Credentials for Tls Servers -
      // 1. Uses FileWatcherCertificateProvider with a refresh interval of 600
      // seconds. (Number decided based on gRPC defaults.
      // 2. Do not ask for client certificates. (Not yet sure what is needed
      // right now.) Add ports to the builders
      experimental::TlsServerCredentialsOptions options(
          std::make_shared<experimental::FileWatcherCertificateProvider>(
              absl::GetFlag(FLAGS_key), absl::GetFlag(FLAGS_crt), 600));
      options.set_cert_request_type(GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE);
      options.watch_identity_key_cert_pairs();
      options.set_check_call_host(false);
      builder.AddListeningPort(server_address, TlsServerCredentials(options));
      LOG(INFO) << "Server listening on " << server_address << " over tls";
    } else {
      builder.AddListeningPort(server_address, InsecureServerCredentials());
      LOG(INFO) << "Server listening on " << server_address << " over insecure";
    }
  }
  // Enable the default health check service, probably not needed though.
  grpc::EnableDefaultHealthCheckService(true);
  std::unique_ptr<Server> xds_server;
  if (has_xds_listeners) {
    xds_server = xds_builder.BuildAndStart();
  }
  std::unique_ptr<Server> server(builder.BuildAndStart());
  server->Wait();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  //  Preprocess argv, for two things:
  //  1. merge duplicate flags. So "--grpc=8080 --grpc=9090" becomes
  //  "--grpc=8080,9090".
  //  2. replace '-' to '_'. So "--istio-version=123" becomes
  //  "--istio_version=123".
  //  3. remove --version since that is specially interpreted by absl
  std::map<std::string, std::vector<std::string>> argv_dict;
  for (int i = 0; i < argc; i++) {
    std::string arg(argv[i]);
    size_t equal = arg.find_first_of('=');
    if (equal != std::string::npos) {
      std::string f = arg.substr(0, equal);
      std::string v = arg.substr(equal + 1, std::string::npos);
      if (f == "--version") {
        argv_dict["--service_version"].push_back(v);
      } else {
        argv_dict[f].push_back(v);
      }
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
  std::set<int> grpc_ports;
  for (const auto& p : absl::GetFlag(FLAGS_grpc)) {
    int grpc_port = std::stoi(p);
    grpc_ports.insert(grpc_port);
  }
  // Create a map of which ports are supposed to use xds
  std::set<int> xds_ports;
  for (const auto& p : absl::GetFlag(FLAGS_xds_grpc_server)) {
    int port = 0;
    if (!absl::SimpleAtoi(p, &port)) {
      LOG(ERROR) << "SimpleAtoi Failure: " << p;
      return 1;
    }
    xds_ports.insert(port);
    // If the port does not exist in gRPC ports set, add it.
    if (grpc_ports.find(port) == grpc_ports.end()) {
      grpc_ports.insert(port);
    }
  }
  // Create a map of which ports are supposed to use tls
  std::set<int> tls_ports;
  for (const auto& p : absl::GetFlag(FLAGS_tls)) {
    int port = 0;
    if (!absl::SimpleAtoi(p, &port)) {
      LOG(ERROR) << "SimpleAtoi Failure: " << p;
      return 1;
    }
    tls_ports.insert(port);
  }
  // Start the servers
  grpc::testing::RunServer(grpc_ports, xds_ports, tls_ports);
  return 0;
}
