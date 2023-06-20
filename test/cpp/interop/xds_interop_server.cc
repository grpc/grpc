//
//
// Copyright 2020 gRPC authors.
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

#include <iostream>

#include "absl/flags/flag.h"

#include <grpc/grpc.h>
#include <grpcpp/health_check_service_interface.h>

#include "src/core/lib/iomgr/gethostname.h"
#include "test/core/util/test_config.h"
#include "test/cpp/interop/xds_interop_server_lib.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(int32_t, port, 8080, "Server port for service.");
ABSL_FLAG(int32_t, maintenance_port, 8081,
          "Server port for maintenance if --security is \"secure\".");
ABSL_FLAG(std::string, server_id, "cpp_server",
          "Server ID to include in responses.");
ABSL_FLAG(bool, secure_mode, false,
          "If true, XdsServerCredentials are used, InsecureServerCredentials "
          "otherwise");

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  char* hostname = grpc_gethostname();
  if (hostname == nullptr) {
    std::cout << "Failed to get hostname, terminating" << std::endl;
    return 1;
  }
  int port = absl::GetFlag(FLAGS_port);
  if (port == 0) {
    std::cout << "Invalid port, terminating" << std::endl;
    return 1;
  }
  int maintenance_port = absl::GetFlag(FLAGS_maintenance_port);
  if (maintenance_port == 0) {
    std::cout << "Invalid maintenance port, terminating" << std::endl;
    return 1;
  }
  grpc::EnableDefaultHealthCheckService(false);
  grpc::testing::RunServer(absl::GetFlag(FLAGS_secure_mode), port,
                           maintenance_port, hostname,
                           absl::GetFlag(FLAGS_server_id));

  return 0;
}
