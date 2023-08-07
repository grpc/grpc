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

#ifdef GPR_POSIX_SUBPROCESS

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>

#include "absl/strings/substitute.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/subprocess.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/strerror.h"

struct gpr_subprocess {
  int pid;
  bool joined;
  int child_stdin_;
  int child_stdout_;
  int child_stderr_;
};

const char* gpr_subprocess_binary_extension() { return ""; }

gpr_subprocess* gpr_subprocess_create(int argc, const char** argv) {
  gpr_subprocess* r;
  int pid;
  char** exec_args;
  int stdin_pipe[2];
  int stdout_pipe[2];
  int stderr_pipe[2];
  int p0 = pipe(stdin_pipe);
  int p1 = pipe(stdout_pipe);
  int p2 = pipe(stderr_pipe);
  GPR_ASSERT(p0 != -1);
  GPR_ASSERT(p1 != -1);
  GPR_ASSERT(p2 != -1);
  pid = fork();
  if (pid == -1) {
    return nullptr;
  } else if (pid == 0) {
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
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
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    r->child_stdin_ = stdin_pipe[1];
    r->child_stdout_ = stdout_pipe[0];
    r->child_stderr_ = stderr_pipe[0];
    return r;
  }
}

gpr_subprocess* gpr_subprocess_create_with_envp(int argc, const char** argv,
                                                int envc, const char** envp) {
  gpr_subprocess* r;
  int pid;
  char **exec_args, **envp_args;
  int stdin_pipe[2];
  int stdout_pipe[2];
  int stderr_pipe[2];
  int p0 = pipe(stdin_pipe);
  int p1 = pipe(stdout_pipe);
  int p2 = pipe(stderr_pipe);
  GPR_ASSERT(p0 != -1);
  GPR_ASSERT(p1 != -1);
  GPR_ASSERT(p2 != -1);
  pid = fork();
  if (pid == -1) {
    return nullptr;
  } else if (pid == 0) {
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    exec_args = static_cast<char**>(
        gpr_malloc((static_cast<size_t>(argc) + 1) * sizeof(char*)));
    memcpy(exec_args, argv, static_cast<size_t>(argc) * sizeof(char*));
    exec_args[argc] = nullptr;
    envp_args = static_cast<char**>(
        gpr_malloc((static_cast<size_t>(envc) + 1) * sizeof(char*)));
    memcpy(envp_args, envp, static_cast<size_t>(envc) * sizeof(char*));
    envp_args[envc] = nullptr;
    execve(exec_args[0], exec_args, envp_args);
    // if we reach here, an error has occurred
    gpr_log(GPR_ERROR, "execvpe '%s' failed: %s", exec_args[0],
            grpc_core::StrError(errno).c_str());
    _exit(1);
  } else {
    r = grpc_core::Zalloc<gpr_subprocess>();
    r->pid = pid;
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    r->child_stdin_ = stdin_pipe[1];
    r->child_stdout_ = stdout_pipe[0];
    r->child_stderr_ = stderr_pipe[0];
    return r;
  }
}

bool gpr_subprocess_communicate(gpr_subprocess* p, std::string& input_data,
                                std::string* output_data,
                                std::string* stderr_data, std::string* error) {
  typedef void SignalHandler(int);

  // Make sure SIGPIPE is disabled so that if the child dies it doesn't kill us.
  SignalHandler* old_pipe_handler = signal(SIGPIPE, SIG_IGN);

  int input_pos = 0;
  int max_fd =
      std::max(p->child_stderr_, std::max(p->child_stdin_, p->child_stdout_));

  while (p->child_stdout_ != -1 || p->child_stderr_ != -1) {
    fd_set read_fds;
    fd_set write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    if (p->child_stdout_ != -1) {
      FD_SET(p->child_stdout_, &read_fds);
    }
    if (p->child_stderr_ != -1) {
      FD_SET(p->child_stderr_, &read_fds);
    }
    if (p->child_stdin_ != -1) {
      FD_SET(p->child_stdin_, &write_fds);
    }

    if (select(max_fd + 1, &read_fds, &write_fds, nullptr, nullptr) < 0) {
      if (errno == EINTR) {
        // Interrupted by signal.  Try again.
        continue;
      } else {
        std::cerr << "select: " << strerror(errno) << std::endl;
        GPR_ASSERT(0);
      }
    }

    if (p->child_stdin_ != -1 && FD_ISSET(p->child_stdin_, &write_fds)) {
      int n = write(p->child_stdin_, input_data.data() + input_pos,
                    input_data.size() - input_pos);
      if (n < 0) {
        // Child closed pipe.  Presumably it will report an error later.
        // Pretend we're done for now.
        input_pos = input_data.size();
      } else {
        input_pos += n;
      }

      if (input_pos == static_cast<int>(input_data.size())) {
        // We're done writing.  Close.
        close(p->child_stdin_);
        p->child_stdin_ = -1;
      }
    }

    if (p->child_stdout_ != -1 && FD_ISSET(p->child_stdout_, &read_fds)) {
      char buffer[4096];
      int n = read(p->child_stdout_, buffer, sizeof(buffer));

      if (n > 0) {
        output_data->append(buffer, static_cast<size_t>(n));
      } else {
        // We're done reading.  Close.
        close(p->child_stdout_);
        p->child_stdout_ = -1;
      }
    }

    if (p->child_stderr_ != -1 && FD_ISSET(p->child_stderr_, &read_fds)) {
      char buffer[4096];
      int n = read(p->child_stderr_, buffer, sizeof(buffer));

      if (n > 0) {
        stderr_data->append(buffer, static_cast<size_t>(n));
      } else {
        // We're done reading.  Close.
        close(p->child_stderr_);
        p->child_stderr_ = -1;
      }
    }
  }

  if (p->child_stdin_ != -1) {
    // Child did not finish reading input before it closed the output.
    // Presumably it exited with an error.
    close(p->child_stdin_);
    p->child_stdin_ = -1;
  }

  int status;
  while (waitpid(p->pid, &status, 0) == -1) {
    if (errno != EINTR) {
      std::cerr << "waitpid: " << strerror(errno) << std::endl;
      GPR_ASSERT(0);
    }
  }

  // Restore SIGPIPE handling.
  signal(SIGPIPE, old_pipe_handler);

  if (WIFEXITED(status)) {
    if (WEXITSTATUS(status) != 0) {
      int error_code = WEXITSTATUS(status);
      *error =
          absl::Substitute("Plugin failed with status code $0.", error_code);
      return false;
    }
  } else if (WIFSIGNALED(status)) {
    int signal = WTERMSIG(status);
    *error = absl::Substitute("Plugin killed by signal $0.", signal);
    return false;
  } else {
    *error = "Neither WEXITSTATUS nor WTERMSIG is true?";
    return false;
  }

  return true;
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
