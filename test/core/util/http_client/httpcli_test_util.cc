//
// Copyright 2015 gRPC authors.
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

#include "test/core/util/http_client/httpcli_test_util.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <string.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/subprocess.h"
#include "test/core/test_util/port.h"

namespace grpc_core {
namespace testing {

HttpRequestTestServer StartHttpRequestTestServer(int argc, char** argv,
                                                 bool use_ssl) {
  int server_port = grpc_pick_unused_port_or_die();
  // Find root path.  The logic is different for bazel vs. cmake.
  std::string root;
  absl::string_view me(argv[0]);
  size_t last_slash = me.rfind('/');
  if (last_slash != me.npos) {
    absl::string_view dirname = me.substr(0, last_slash);
    if (absl::EndsWith(dirname, "/http_client")) {
      // Bazel paths will end in "test/core/util/http_client".
      root = absl::StrCat(dirname, "/../../../..");
    } else {
      // Cmake paths will be "cmake/build".
      root = absl::StrCat(dirname, "/../..");
    }
  } else {
    root = ".";
  }
  // Construct args.
  std::vector<const char*> args;
  std::string python_wrapper_path;
  std::string test_server_path;
  CHECK_LE(argc, 2);
  if (argc == 2) {
    args.push_back(argv[1]);
  } else {
    python_wrapper_path =
        absl::StrCat(root, "/test/core/util/http_client/python_wrapper.sh");
    test_server_path =
        absl::StrCat(root, "/test/core/util/http_client/test_server.py");
    args.push_back(python_wrapper_path.c_str());
    args.push_back(test_server_path.c_str());
  }
  args.push_back("--port");
  std::string port_number = absl::StrCat(server_port);
  args.push_back(port_number.c_str());
  if (use_ssl) {
    args.push_back("--ssl");
    // Set the environment variable for the SSL certificate file.
    ConfigVars::Overrides overrides;
    overrides.default_ssl_roots_file_path =
        absl::StrCat(root, "/src/core/tsi/test_creds/ca.pem");
    ConfigVars::SetOverrides(overrides);
  }
  LOG(INFO) << "starting HttpRequest test server subprocess:";
  for (size_t i = 0; i < args.size(); i++) {
    LOG(INFO) << "  HttpRequest test server subprocess argv[" << i
              << "]: " << args[i];
  }
  // Start the server.
  gpr_subprocess* server = gpr_subprocess_create(args.size(), args.data());
  CHECK(server);
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_seconds(5, GPR_TIMESPAN)));
  return {server, server_port};
}

}  // namespace testing
}  // namespace grpc_core
