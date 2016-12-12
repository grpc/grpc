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

#include <memory>
#include <mutex>

#include <grpc++/impl/codegen/method_handler_impl.h>
#include <grpc/support/log.h>

#include "src/cpp/server/default_health_check_service.h"

namespace grpc {
namespace {

const char kHealthCheckMethodName[] = "/grpc.health.v1.Health/Check";

}  // namespace

DefaultHealthCheckService::SyncHealthCheckServiceImpl::
    SyncHealthCheckServiceImpl(DefaultHealthCheckService* service)
    : service_(service) {
  auto* handler =
      new RpcMethodHandler<SyncHealthCheckServiceImpl, ByteBuffer, ByteBuffer>(
          std::mem_fn(&SyncHealthCheckServiceImpl::Check), this);
  auto* method = new RpcServiceMethod(kHealthCheckMethodName,
                                      RpcMethod::NORMAL_RPC, handler);
  AddMethod(method);
}

Status DefaultHealthCheckService::SyncHealthCheckServiceImpl::Check(
    ServerContext* context, const ByteBuffer* request, ByteBuffer* response) {
  // TODO nanopb part
  return Status::OK;
}

DefaultHealthCheckService::DefaultHealthCheckService()
    : sync_service_(new SyncHealthCheckServiceImpl(this)) {
  services_map_.emplace("", true);
}

void DefaultHealthCheckService::SetServingStatus(
    const grpc::string& service_name, bool serving) {
  std::lock_guard<std::mutex> lock(mu_);
  services_map_[service_name] = serving;
}

void DefaultHealthCheckService::SetServingStatus(bool serving) {
  std::lock_guard<std::mutex> lock(mu_);
  for (auto iter = services_map_.begin(); iter != services_map_.end(); ++iter) {
    iter->second = serving;
  }
}

DefaultHealthCheckService::ServingStatus
DefaultHealthCheckService::GetServingStatus(const grpc::string& service_name) {
  std::lock_guard<std::mutex> lock(mu_);
  const auto& iter = services_map_.find(service_name);
  if (iter == services_map_.end()) {
    return NOT_FOUND;
  }
  return iter->second ? SERVING : NOT_SERVING;
}

}  // namespace grpc
