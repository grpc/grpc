/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GPR_WINDOWS

#include <grpc/support/log.h>
#include <signal.h>
#include <string.h>

#include "test/cpp/naming/resolver_component_tests_runner_invoker.h"
#include "test/cpp/util/test_config.h"

namespace grpc {

namespace testing {

bool kResolverComponentTestsWindows = false;

void ResolverComponentTestsRegisterSigHandler(void (*sighandler)(int)) {
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = sighandler;
  sigaction(SIGINT, &act, nullptr);
  sigaction(SIGTERM, &act, nullptr);
}

void CheckResolverComponentTestRunnerExitStatus(int status) {
  if (WIFEXITED(status)) {
    if (WEXITSTATUS(status)) {
      gpr_log(GPR_INFO,
              "Resolver component test test-runner exited with code %d",
              WEXITSTATUS(status));
      abort();
    }
  } else if (WIFSIGNALED(status)) {
    gpr_log(GPR_INFO,
            "Resolver component test test-runner ended from signal %d",
            WTERMSIG(status));
    abort();
  } else {
    gpr_log(GPR_INFO,
            "Resolver component test test-runner ended with unknown status %d",
            status);
    abort();
  }
}

}  // namespace testing

}  // namespace grpc

#endif  /// #ifndef GPR_WINDOWS
