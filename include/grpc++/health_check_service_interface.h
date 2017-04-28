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

#ifndef GRPCXX_HEALTH_CHECK_SERVICE_INTERFACE_H
#define GRPCXX_HEALTH_CHECK_SERVICE_INTERFACE_H

#include <grpc++/support/config.h>

namespace grpc {

const char kHealthCheckServiceInterfaceArg[] =
    "grpc.health_check_service_interface";

// The gRPC server uses this interface to expose the health checking service
// without depending on protobuf.
class HealthCheckServiceInterface {
 public:
  virtual ~HealthCheckServiceInterface() {}

  // Set or change the serving status of the given service_name.
  virtual void SetServingStatus(const grpc::string& service_name,
                                bool serving) = 0;
  // Apply to all registered service names.
  virtual void SetServingStatus(bool serving) = 0;
};

// Enable/disable the default health checking service. This applies to all C++
// servers created afterwards. For each server, user can override the default
// with a HealthCheckServiceServerBuilderOption.
// NOT thread safe.
void EnableDefaultHealthCheckService(bool enable);

// NOT thread safe.
bool DefaultHealthCheckServiceEnabled();

}  // namespace grpc

#endif  // GRPCXX_HEALTH_CHECK_SERVICE_INTERFACE_H
