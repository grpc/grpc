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

#ifndef GRPCXX_EXT_HEALTH_CHECK_SERVICE_SERVER_BUILDER_OPTION_H
#define GRPCXX_EXT_HEALTH_CHECK_SERVICE_SERVER_BUILDER_OPTION_H

#include <memory>

#include <grpc++/health_check_service_interface.h>
#include <grpc++/impl/server_builder_option.h>
#include <grpc++/support/config.h>

namespace grpc {

class HealthCheckServiceServerBuilderOption : public ServerBuilderOption {
 public:
  // The ownership of hc will be taken and transferred to the grpc server.
  // To explicitly disable default service, pass in a nullptr.
  explicit HealthCheckServiceServerBuilderOption(
      std::unique_ptr<HealthCheckServiceInterface> hc);
  ~HealthCheckServiceServerBuilderOption() override {}
  void UpdateArguments(ChannelArguments* args) override;
  void UpdatePlugins(
      std::vector<std::unique_ptr<ServerBuilderPlugin>>* plugins) override;

 private:
  std::unique_ptr<HealthCheckServiceInterface> hc_;
};

}  // namespace grpc

#endif  // GRPCXX_EXT_HEALTH_CHECK_SERVICE_SERVER_BUILDER_OPTION_H
