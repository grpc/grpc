//
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
//

#include <grpc/support/port_platform.h>

#include <string>

#ifdef GPR_POSIX_SUBPROCESS

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/strerror.h"
#include "test/core/util/subprocess.h"

struct gpr_subprocess {
  int pid;
  bool joined;
};

const char* gpr_subprocess_binary_extension() { return ""; }

gpr_subprocess* gpr_subprocess_create(int argc, const char** argv) {
  gpr_subprocess* r;
  int pid;
  char** exec_args;

  pid = fork();
  if (pid == -1) {
    return nullptr;
  } else if (pid == 0) {
    exec_args = static_cast<char**>(
        gpr_malloc((static_cast<size_t>(argc) + 1) * sizeof(char*)));
    memcpy(exec_args, argv, static_cast<size_t>(argc) * sizeof(char*));
    exec_args[argc] = nullptr;
    execv(exec_args[0], exec_args);
    // if we reach here, an error has occurred
    gpr_log(GPR_ERROR, "execv '%s' failed: %s", exec_args[0],
            grpc_core::StrError(errno).c_str());
    _exit(1);
  } else {
    r = grpc_core::Zalloc<gpr_subprocess>();
    r->pid = pid;
    return r;
  }
}

void gpr_subprocess_destroy(gpr_subprocess* p) {
  if (!p->joined) {
    kill(p->pid, SIGKILL);
    gpr_subprocess_join(p);
  }
  gpr_free(p);
}

int gpr_subprocess_join(gpr_subprocess* p) {
  int status;
retry:
  if (waitpid(p->pid, &status, 0) == -1) {
    if (errno == EINTR) {
      goto retry;
    }
    gpr_log(GPR_ERROR, "waitpid failed for pid %d: %s", p->pid,
            grpc_core::StrError(errno).c_str());
    return -1;
  }
  p->joined = true;
  return status;
}

void gpr_subprocess_interrupt(gpr_subprocess* p) {
  if (!p->joined) {
    kill(p->pid, SIGINT);
  }
}

int gpr_subprocess_get_process_id(gpr_subprocess* p) { return p->pid; }

#endif  // GPR_POSIX_SUBPROCESS
