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
#include <grpcpp/support/string_ref.h>

#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/iomgr/gethostname.h"
#include "src/proto/grpc/testing/istio_echo.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/interop/istio_echo_server_lib.h"
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

namespace grpc {
namespace testing {
namespace {
/*std::vector<std::unique_ptr<grpc::Server>>*/
void RunServer(std::vector<int> ports) {
  std::string hostname;
  char* hostname_p = grpc_gethostname();
  if (hostname_p == nullptr) {
    hostname = absl::StrFormat("generated-%d", rand() % 1000);
  } else {
    hostname = hostname_p;
    free(hostname_p);
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

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  // Preprocess argv, for two things:
  // 1. merge duplciate flags. So "--grpc=8080 --grpc=9090" becomes
  // "--grpc=8080,9090".
  // 2. replace '-' to '_'. So "--istio-version=123" becomes
  // "--istio_version=123".
  std::map<std::string, std::vector<std::string>> argv_dict;
  for (int i = 0; i < argc; i++) {
    std::string arg(argv[i]);
    size_t equal = arg.find_first_of('=');
    if (equal != std::string::npos) {
      std::string f = arg.substr(0, equal);
      if (f == "--version") {
        continue;
      }
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
  grpc::testing::RunServer(grpc_ports);
  return 0;
}
