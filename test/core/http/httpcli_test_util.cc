/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "test/core/http/httpcli_test_util.h"

#include <string.h>

#include <tuple>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

std::tuple<gpr_subprocess*, int> StartHttpCliTestServer(int argc, char** argv, bool /*use_ssl*/) {
    gpr_log(GPR_INFO, "begin SetUpTestSuite");
    char* me = argv[0];
    char* lslash = strrchr(me, '/');
    char* args[4];
    gpr_log(GPR_INFO, "begin SetUpTestSuite 1");
    int server_port = grpc_pick_unused_port_or_die();
    int arg_shift = 0;
    /* figure out where we are */
    gpr_log(GPR_INFO, "begin SetUpTestSuite 2");
    char* root;
    if (lslash != nullptr) {
      /* Hack for bazel target */
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
    gpr_log(GPR_INFO, "begin SetUpTestSuite 3");

    GPR_ASSERT(argc <= 2);
    if (argc == 2) {
      args[0] = gpr_strdup(argv[1]);
    } else {
      arg_shift = 1;
      gpr_asprintf(&args[0], "%s/test/core/http/python_wrapper.sh", root);
      gpr_asprintf(&args[1], "%s/test/core/http/test_server.py", root);
    }
    gpr_log(GPR_INFO, "begin SetUpTestSuite 4");

    /* start the server */
    args[1 + arg_shift] = const_cast<char*>("--port");
    gpr_asprintf(&args[2 + arg_shift], "%d", server_port);
    int num_args = 3 + arg_shift;
    gpr_log(GPR_INFO, "starting test server subprocess:");
    for (int i = 0; i < num_args; i++) {
      gpr_log(GPR_INFO, "  test server subprocess argv[%d]: %s", i, args[i]);
    }
    gpr_subprocess* server =
        gpr_subprocess_create(3 + arg_shift, const_cast<const char**>(args));
    GPR_ASSERT(server);
    gpr_free(args[0]);
    if (arg_shift) gpr_free(args[1]);
    gpr_free(args[2 + arg_shift]);
    gpr_free(root);
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_seconds(5, GPR_TIMESPAN)));
    gpr_log(GPR_INFO, "begin SetUpTestSuite 5");
    return std::make_tuple(server, server_port);
  }

} // namespace testing
} // namespace grpc_core
