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

#include <grpc/support/port_platform.h>

#include "test/core/http/httpcli_test_util.h"

#include <string.h>

#include <tuple>
#include <vector>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/security/security_connector/ssl_utils_config.h"
#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

HttpRequestTestServer StartHttpRequestTestServer(int argc, char** argv,
                                                 bool use_ssl) {
  char* me = argv[0];
  char* lslash = strrchr(me, '/');
  std::vector<char*> args;
  int server_port = grpc_pick_unused_port_or_die();
  // figure out where we are
  char* root;
  if (lslash != nullptr) {
    // Hack for bazel target
    if (static_cast<unsigned>(lslash - me) >= (sizeof("http") - 1) &&
        strncmp(me + (lslash - me) - sizeof("http") + 1, "http",
                sizeof("http") - 1) == 0) {
      lslash = me + (lslash - me) - sizeof("http");
    }
    root = static_cast<char*>(
        gpr_malloc(static_cast<size_t>(lslash - me + sizeof("/../.."))));
    memcpy(root, me, static_cast<size_t>(lslash - me));
    memcpy(root + (lslash - me), "/../..", sizeof("/../.."));
  } else {
    root = gpr_strdup(".");
  }
  GPR_ASSERT(argc <= 2);
  if (argc == 2) {
    args.push_back(gpr_strdup(argv[1]));
  } else {
    char* python_wrapper_arg;
    char* test_server_arg;
    gpr_asprintf(&python_wrapper_arg, "%s/test/core/http/python_wrapper.sh",
                 root);
    gpr_asprintf(&test_server_arg, "%s/test/core/http/test_server.py", root);
    args.push_back(python_wrapper_arg);
    args.push_back(test_server_arg);
  }
  // start the server
  args.push_back(gpr_strdup("--port"));
  char* server_port_str;
  gpr_asprintf(&server_port_str, "%d", server_port);
  args.push_back(server_port_str);
  if (use_ssl) {
    args.push_back(gpr_strdup("--ssl"));
    // Set the environment variable for the SSL certificate file
    char* pem_file;
    gpr_asprintf(&pem_file, "%s/src/core/tsi/test_creds/ca.pem", root);
    GPR_GLOBAL_CONFIG_SET(grpc_default_ssl_roots_file_path, pem_file);
    gpr_free(pem_file);
  }
  gpr_log(GPR_INFO, "starting HttpRequest test server subprocess:");
  for (size_t i = 0; i < args.size(); i++) {
    gpr_log(GPR_INFO, "  HttpRequest test server subprocess argv[%ld]: %s", i,
            args[i]);
  }
  gpr_subprocess* server =
      gpr_subprocess_create(args.size(), const_cast<const char**>(args.data()));
  GPR_ASSERT(server);
  for (size_t i = 0; i < args.size(); i++) {
    gpr_free(args[i]);
  }
  gpr_free(root);
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_seconds(5, GPR_TIMESPAN)));
  return {server, server_port};
}

}  // namespace testing
}  // namespace grpc_core
