/*
 * Copyright (c) 2009-2022, Google LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Google LLC nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL Google LLC BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Shamelessly copied from the protobuf compiler's subprocess.cc
// except this version passes strings instead of Messages.

#include "upbc/subprocess.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#ifndef _MSVC_LANG
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#endif

#include "absl/log/absl_log.h"
#include "absl/strings/substitute.h"

// Must be last.
#include "upb/port/def.inc"

namespace upbc {

namespace {
char* portable_strdup(const char* s) {
  char* ns = (char*)malloc(strlen(s) + 1);
  if (ns != nullptr) {
    strcpy(ns, s);
  }
  return ns;
}
}  // namespace

#ifdef _WIN32

static void CloseHandleOrDie(HANDLE handle) {
  if (!CloseHandle(handle)) {
    ABSL_LOG(FATAL) << "CloseHandle: "
                    << Subprocess::Win32ErrorMessage(GetLastError());
  }
}

Subprocess::Subprocess()
    : process_start_error_(ERROR_SUCCESS),
      child_handle_(nullptr),
      child_stdin_(nullptr),
      child_stdout_(nullptr) {}

Subprocess::~Subprocess() {
  if (child_stdin_ != nullptr) {
    CloseHandleOrDie(child_stdin_);
  }
  if (child_stdout_ != nullptr) {
    CloseHandleOrDie(child_stdout_);
  }
}

void Subprocess::Start(const std::string& program, SearchMode search_mode) {
  // Create the pipes.
  HANDLE stdin_pipe_read;
  HANDLE stdin_pipe_write;
  HANDLE stdout_pipe_read;
  HANDLE stdout_pipe_write;

  if (!CreatePipe(&stdin_pipe_read, &stdin_pipe_write, nullptr, 0)) {
    ABSL_LOG(FATAL) << "CreatePipe: " << Win32ErrorMessage(GetLastError());
  }
  if (!CreatePipe(&stdout_pipe_read, &stdout_pipe_write, nullptr, 0)) {
    ABSL_LOG(FATAL) << "CreatePipe: " << Win32ErrorMessage(GetLastError());
  }

  // Make child side of the pipes inheritable.
  if (!SetHandleInformation(stdin_pipe_read, HANDLE_FLAG_INHERIT,
                            HANDLE_FLAG_INHERIT)) {
    ABSL_LOG(FATAL) << "SetHandleInformation: "
                    << Win32ErrorMessage(GetLastError());
  }
  if (!SetHandleInformation(stdout_pipe_write, HANDLE_FLAG_INHERIT,
                            HANDLE_FLAG_INHERIT)) {
    ABSL_LOG(FATAL) << "SetHandleInformation: "
                    << Win32ErrorMessage(GetLastError());
  }

  // Setup STARTUPINFO to redirect handles.
  STARTUPINFOA startup_info;
  ZeroMemory(&startup_info, sizeof(startup_info));
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdInput = stdin_pipe_read;
  startup_info.hStdOutput = stdout_pipe_write;
  startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  if (startup_info.hStdError == INVALID_HANDLE_VALUE) {
    ABSL_LOG(FATAL) << "GetStdHandle: " << Win32ErrorMessage(GetLastError());
  }

  // Invoking cmd.exe allows for '.bat' files from the path as well as '.exe'.
  // Using a malloc'ed string because CreateProcess() can mutate its second
  // parameter.
  char* command_line =
      portable_strdup(("cmd.exe /c \"" + program + "\"").c_str());

  // Create the process.
  PROCESS_INFORMATION process_info;

  if (CreateProcessA((search_mode == SEARCH_PATH) ? nullptr : program.c_str(),
                     (search_mode == SEARCH_PATH) ? command_line : nullptr,
                     nullptr,  // process security attributes
                     nullptr,  // thread security attributes
                     TRUE,     // inherit handles?
                     0,        // obscure creation flags
                     nullptr,  // environment (inherit from parent)
                     nullptr,  // current directory (inherit from parent)
                     &startup_info, &process_info)) {
    child_handle_ = process_info.hProcess;
    CloseHandleOrDie(process_info.hThread);
    child_stdin_ = stdin_pipe_write;
    child_stdout_ = stdout_pipe_read;
  } else {
    process_start_error_ = GetLastError();
    CloseHandleOrDie(stdin_pipe_write);
    CloseHandleOrDie(stdout_pipe_read);
  }

  CloseHandleOrDie(stdin_pipe_read);
  CloseHandleOrDie(stdout_pipe_write);
  free(command_line);
}

bool Subprocess::Communicate(const std::string& input_data,
                             std::string* output_data, std::string* error) {
  if (process_start_error_ != ERROR_SUCCESS) {
    *error = Win32ErrorMessage(process_start_error_);
    return false;
  }

  GOOGLE_CHECK(child_handle_ != nullptr) << "Must call Start() first.";

  int input_pos = 0;

  while (child_stdout_ != nullptr) {
    HANDLE handles[2];
    int handle_count = 0;

    if (child_stdin_ != nullptr) {
      handles[handle_count++] = child_stdin_;
    }
    if (child_stdout_ != nullptr) {
      handles[handle_count++] = child_stdout_;
    }

    DWORD wait_result =
        WaitForMultipleObjects(handle_count, handles, FALSE, INFINITE);

    HANDLE signaled_handle = nullptr;
    if (wait_result >= WAIT_OBJECT_0 &&
        wait_result < WAIT_OBJECT_0 + handle_count) {
      signaled_handle = handles[wait_result - WAIT_OBJECT_0];
    } else if (wait_result == WAIT_FAILED) {
      ABSL_LOG(FATAL) << "WaitForMultipleObjects: "
                      << Win32ErrorMessage(GetLastError());
    } else {
      ABSL_LOG(FATAL) << "WaitForMultipleObjects: Unexpected return code: "
                      << wait_result;
    }

    if (signaled_handle == child_stdin_) {
      DWORD n;
      if (!WriteFile(child_stdin_, input_data.data() + input_pos,
                     input_data.size() - input_pos, &n, nullptr)) {
        // Child closed pipe.  Presumably it will report an error later.
        // Pretend we're done for now.
        input_pos = input_data.size();
      } else {
        input_pos += n;
      }

      if (input_pos == input_data.size()) {
        // We're done writing.  Close.
        CloseHandleOrDie(child_stdin_);
        child_stdin_ = nullptr;
      }
    } else if (signaled_handle == child_stdout_) {
      char buffer[4096];
      DWORD n;

      if (!ReadFile(child_stdout_, buffer, sizeof(buffer), &n, nullptr)) {
        // We're done reading.  Close.
        CloseHandleOrDie(child_stdout_);
        child_stdout_ = nullptr;
      } else {
        output_data->append(buffer, n);
      }
    }
  }

  if (child_stdin_ != nullptr) {
    // Child did not finish reading input before it closed the output.
    // Presumably it exited with an error.
    CloseHandleOrDie(child_stdin_);
    child_stdin_ = nullptr;
  }

  DWORD wait_result = WaitForSingleObject(child_handle_, INFINITE);

  if (wait_result == WAIT_FAILED) {
    ABSL_LOG(FATAL) << "WaitForSingleObject: "
                    << Win32ErrorMessage(GetLastError());
  } else if (wait_result != WAIT_OBJECT_0) {
    ABSL_LOG(FATAL) << "WaitForSingleObject: Unexpected return code: "
                    << wait_result;
  }

  DWORD exit_code;
  if (!GetExitCodeProcess(child_handle_, &exit_code)) {
    ABSL_LOG(FATAL) << "GetExitCodeProcess: "
                    << Win32ErrorMessage(GetLastError());
  }

  CloseHandleOrDie(child_handle_);
  child_handle_ = nullptr;

  if (exit_code != 0) {
    *error = absl::Substitute("Plugin failed with status code $0.", exit_code);
    return false;
  }

  return true;
}

std::string Subprocess::Win32ErrorMessage(DWORD error_code) {
  char* message;

  // WTF?
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, error_code,
                 MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                 (LPSTR)&message,  // NOT A BUG!
                 0, nullptr);

  std::string result = message;
  LocalFree(message);
  return result;
}

// ===================================================================

#else  // _WIN32

Subprocess::Subprocess()
    : child_pid_(-1), child_stdin_(-1), child_stdout_(-1) {}

Subprocess::~Subprocess() {
  if (child_stdin_ != -1) {
    close(child_stdin_);
  }
  if (child_stdout_ != -1) {
    close(child_stdout_);
  }
}

void Subprocess::Start(const std::string& program, SearchMode search_mode) {
  // Note that we assume that there are no other threads, thus we don't have to
  // do crazy stuff like using socket pairs or avoiding libc locks.

  // [0] is read end, [1] is write end.
  int stdin_pipe[2];
  int stdout_pipe[2];

  int p0 = pipe(stdin_pipe);
  int p1 = pipe(stdout_pipe);
  UPB_ASSERT(p0 != -1);
  UPB_ASSERT(p1 != -1);

  char* argv[2] = {portable_strdup(program.c_str()), nullptr};

  child_pid_ = fork();
  if (child_pid_ == -1) {
    std::cerr << "fork: " << strerror(errno);
  } else if (child_pid_ == 0) {
    // We are the child.
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);

    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);

    switch (search_mode) {
      case SEARCH_PATH:
        execvp(argv[0], argv);
        break;
      case EXACT_NAME:
        execv(argv[0], argv);
        break;
    }

    // Write directly to STDERR_FILENO to avoid stdio code paths that may do
    // stuff that is unsafe here.
    int ignored;
    ignored = write(STDERR_FILENO, argv[0], strlen(argv[0]));
    const char* message =
        ": program not found or is not executable\n"
        "Please specify a program using absolute path or make sure "
        "the program is available in your PATH system variable\n";
    ignored = write(STDERR_FILENO, message, strlen(message));
    (void)ignored;

    // Must use _exit() rather than exit() to avoid flushing output buffers
    // that will also be flushed by the parent.
    _exit(1);
  } else {
    free(argv[0]);

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    child_stdin_ = stdin_pipe[1];
    child_stdout_ = stdout_pipe[0];
  }
}

bool Subprocess::Communicate(const std::string& input_data,
                             std::string* output_data, std::string* error) {
  if (child_stdin_ == -1) {
    std::cerr << "Must call Start() first." << std::endl;
    UPB_ASSERT(child_stdin_ != -1);
  }

  // The "sighandler_t" typedef is GNU-specific, so define our own.
  typedef void SignalHandler(int);

  // Make sure SIGPIPE is disabled so that if the child dies it doesn't kill us.
  SignalHandler* old_pipe_handler = signal(SIGPIPE, SIG_IGN);

  int input_pos = 0;
  int max_fd = std::max(child_stdin_, child_stdout_);

  while (child_stdout_ != -1) {
    fd_set read_fds;
    fd_set write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    if (child_stdout_ != -1) {
      FD_SET(child_stdout_, &read_fds);
    }
    if (child_stdin_ != -1) {
      FD_SET(child_stdin_, &write_fds);
    }

    if (select(max_fd + 1, &read_fds, &write_fds, nullptr, nullptr) < 0) {
      if (errno == EINTR) {
        // Interrupted by signal.  Try again.
        continue;
      } else {
        std::cerr << "select: " << strerror(errno) << std::endl;
        UPB_ASSERT(0);
      }
    }

    if (child_stdin_ != -1 && FD_ISSET(child_stdin_, &write_fds)) {
      int n = write(child_stdin_, input_data.data() + input_pos,
                    input_data.size() - input_pos);
      if (n < 0) {
        // Child closed pipe.  Presumably it will report an error later.
        // Pretend we're done for now.
        input_pos = input_data.size();
      } else {
        input_pos += n;
      }

      if (input_pos == (int)input_data.size()) {
        // We're done writing.  Close.
        close(child_stdin_);
        child_stdin_ = -1;
      }
    }

    if (child_stdout_ != -1 && FD_ISSET(child_stdout_, &read_fds)) {
      char buffer[4096];
      int n = read(child_stdout_, buffer, sizeof(buffer));

      if (n > 0) {
        output_data->append(buffer, (size_t)n);
      } else {
        // We're done reading.  Close.
        close(child_stdout_);
        child_stdout_ = -1;
      }
    }
  }

  if (child_stdin_ != -1) {
    // Child did not finish reading input before it closed the output.
    // Presumably it exited with an error.
    close(child_stdin_);
    child_stdin_ = -1;
  }

  int status;
  while (waitpid(child_pid_, &status, 0) == -1) {
    if (errno != EINTR) {
      std::cerr << "waitpid: " << strerror(errno) << std::endl;
      UPB_ASSERT(0);
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

#endif  // !_WIN32

}  // namespace upbc
