/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_TEST_CPP_UTIL_GRPC_TOOL_H
#define GRPC_TEST_CPP_UTIL_GRPC_TOOL_H

#include <functional>

#include <grpc++/support/config.h>

#include "test/cpp/util/cli_credentials.h"

namespace grpc {
namespace testing {

typedef std::function<bool(const grpc::string &)> GrpcToolOutputCallback;

class GrpcTool {
 public:
  GrpcTool();
  virtual ~GrpcTool() {}

  int GrpcToolMainLib(int argc, const char** argv, const CliCredentials& cred,
                      GrpcToolOutputCallback callback);

 protected:
  virtual std::istream* input_stream() {
    std::cout << "call base" << std::endl;
    return input_stream_;
  }

 private:
  bool Help(int argc, const char** argv, const CliCredentials& cred,
            GrpcToolOutputCallback callback);
  bool CallMethod(int argc, const char** argv, const CliCredentials& cred,
                  GrpcToolOutputCallback callback);
  bool ListServices(int argc, const char** argv, const CliCredentials& cred,
                    GrpcToolOutputCallback callback);
  bool PrintType(int argc, const char** argv, const CliCredentials& cred,
                 GrpcToolOutputCallback callback);
  bool ParseMessage(int argc, const char** argv, const CliCredentials& cred,
                    GrpcToolOutputCallback callback);
  bool ToText(int argc, const char** argv, const CliCredentials& cred,
              GrpcToolOutputCallback callback);
  bool ToBinary(int argc, const char** argv, const CliCredentials& cred,
                GrpcToolOutputCallback callback);

  void SetPrintCommandMode(int exit_status) {
    print_command_usage_ = true;
    usage_exit_status_ = exit_status;
  }
  void CommandUsage(const grpc::string& usage) const;
  void SetInputStream(std::istream* input_stream) {
    input_stream_ = input_stream;
  }

  typedef std::function<bool(GrpcTool*, int, const char**,
                             const CliCredentials&, GrpcToolOutputCallback)>
      CommandFunction;

  struct Command {
    const char* command;
    CommandFunction function;
    int min_args;
    int max_args;
  };

  const Command* FindCommand(const grpc::string& name);

  template <typename T>
  CommandFunction static BindWith5Args(T&& func) {
    return std::bind(std::forward<T>(func), std::placeholders::_1,
                     std::placeholders::_2, std::placeholders::_3,
                     std::placeholders::_4, std::placeholders::_5);
  }

  bool print_command_usage_;
  int usage_exit_status_;
  const grpc::string cred_usage_;
  std::istream* input_stream_;
  static const Command ops_[];
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_GRPC_TOOL_H
