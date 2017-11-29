/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_INTERNAL_CPP_SERVER_DEFAULT_HEALTH_CHECK_SERVICE_H
#define GRPC_INTERNAL_CPP_SERVER_DEFAULT_HEALTH_CHECK_SERVICE_H

#include <mutex>

#include <grpc++/health_check_service_interface.h>
#include <grpc++/impl/codegen/service_type.h>
#include <grpc++/support/byte_buffer.h>

namespace grpc {

// Default implementation of HealthCheckServiceInterface. Server will create and
// own it.
class DefaultHealthCheckService final : public HealthCheckServiceInterface {
 public:
  // The service impl to register with the server.
  class HealthCheckServiceImpl : public Service {
   public:
    explicit HealthCheckServiceImpl(DefaultHealthCheckService* service);

    Status Check(ServerContext* context, const ByteBuffer* request,
                 ByteBuffer* response);

   private:
    const DefaultHealthCheckService* const service_;
    internal::RpcServiceMethod* method_;
  };

  DefaultHealthCheckService();
  void SetServingStatus(const grpc::string& service_name,
                        bool serving) override;
  void SetServingStatus(bool serving) override;
  enum ServingStatus { NOT_FOUND, SERVING, NOT_SERVING };
  ServingStatus GetServingStatus(const grpc::string& service_name) const;
  HealthCheckServiceImpl* GetHealthCheckService();

 private:
  mutable std::mutex mu_;
  std::map<grpc::string, bool> services_map_;
  std::unique_ptr<HealthCheckServiceImpl> impl_;
};

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_SERVER_DEFAULT_HEALTH_CHECK_SERVICE_H
