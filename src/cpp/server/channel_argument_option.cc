/*
 *
 * Copyright 2017, Google Inc.
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

#include <grpc++/impl/channel_argument_option.h>

namespace grpc {

std::unique_ptr<ServerBuilderOption> MakeChannelArgumentOption(
    const grpc::string &name, const grpc::string &value) {
  class StringOption final : public ServerBuilderOption {
   public:
    StringOption(const grpc::string &name, const grpc::string &value)
        : name_(name), value_(value) {}

    virtual void UpdateArguments(ChannelArguments *args) override {
      args->SetString(name_, value_);
    }
    virtual void UpdatePlugins(
        std::vector<std::unique_ptr<ServerBuilderPlugin>> *plugins) override {}

   private:
    const grpc::string name_;
    const grpc::string value_;
  };
  return std::unique_ptr<ServerBuilderOption>(new StringOption(name, value));
}

std::unique_ptr<ServerBuilderOption> MakeChannelArgumentOption(
    const grpc::string &name, int value) {
  class IntOption final : public ServerBuilderOption {
   public:
    IntOption(const grpc::string &name, int value)
        : name_(name), value_(value) {}

    virtual void UpdateArguments(ChannelArguments *args) override {
      args->SetInt(name_, value_);
    }
    virtual void UpdatePlugins(
        std::vector<std::unique_ptr<ServerBuilderPlugin>> *plugins) override {}

   private:
    const grpc::string name_;
    const int value_;
  };
  return std::unique_ptr<ServerBuilderOption>(new IntOption(name, value));
}

}  // namespace grpc
