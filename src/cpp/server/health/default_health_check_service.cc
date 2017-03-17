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
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/cpp/server/health/default_health_check_service.h"
#include "src/cpp/server/health/health.pb.h"
#include "third_party/nanopb/pb_decode.h"
#include "third_party/nanopb/pb_encode.h"

namespace grpc {
namespace {
const char kHealthCheckMethodName[] = "/grpc.health.v1.Health/Check";
}  // namespace

DefaultHealthCheckService::HealthCheckServiceImpl::HealthCheckServiceImpl(
    DefaultHealthCheckService* service)
    : service_(service), method_(nullptr) {
  MethodHandler* handler =
      new RpcMethodHandler<HealthCheckServiceImpl, ByteBuffer, ByteBuffer>(
          std::mem_fn(&HealthCheckServiceImpl::Check), this);
  method_ = new RpcServiceMethod(kHealthCheckMethodName, RpcMethod::NORMAL_RPC,
                                 handler);
  AddMethod(method_);
}

Status DefaultHealthCheckService::HealthCheckServiceImpl::Check(
    ServerContext* context, const ByteBuffer* request, ByteBuffer* response) {
  // Decode request.
  std::vector<Slice> slices;
  if (!request->Dump(&slices).ok()) {
    return Status(StatusCode::INVALID_ARGUMENT, "");
  }
  uint8_t* request_bytes = nullptr;
  bool request_bytes_owned = false;
  size_t request_size = 0;
  grpc_health_v1_HealthCheckRequest request_struct;
  if (slices.empty()) {
    request_struct.has_service = false;
  } else if (slices.size() == 1) {
    request_bytes = const_cast<uint8_t*>(slices[0].begin());
    request_size = slices[0].size();
  } else {
    request_bytes_owned = true;
    request_bytes = static_cast<uint8_t*>(gpr_malloc(request->Length()));
    uint8_t* copy_to = request_bytes;
    for (size_t i = 0; i < slices.size(); i++) {
      memcpy(copy_to, slices[i].begin(), slices[i].size());
      copy_to += slices[i].size();
    }
  }

  if (request_bytes != nullptr) {
    pb_istream_t istream = pb_istream_from_buffer(request_bytes, request_size);
    bool decode_status = pb_decode(
        &istream, grpc_health_v1_HealthCheckRequest_fields, &request_struct);
    if (request_bytes_owned) {
      gpr_free(request_bytes);
    }
    if (!decode_status) {
      return Status(StatusCode::INVALID_ARGUMENT, "");
    }
  }

  // Check status from the associated default health checking service.
  DefaultHealthCheckService::ServingStatus serving_status =
      service_->GetServingStatus(
          request_struct.has_service ? request_struct.service : "");
  if (serving_status == DefaultHealthCheckService::NOT_FOUND) {
    return Status(StatusCode::NOT_FOUND, "");
  }

  // Encode response
  grpc_health_v1_HealthCheckResponse response_struct;
  response_struct.has_status = true;
  response_struct.status =
      serving_status == DefaultHealthCheckService::SERVING
          ? grpc_health_v1_HealthCheckResponse_ServingStatus_SERVING
          : grpc_health_v1_HealthCheckResponse_ServingStatus_NOT_SERVING;
  pb_ostream_t ostream;
  memset(&ostream, 0, sizeof(ostream));
  pb_encode(&ostream, grpc_health_v1_HealthCheckResponse_fields,
            &response_struct);
  grpc_slice response_slice = grpc_slice_malloc(ostream.bytes_written);
  ostream = pb_ostream_from_buffer(GRPC_SLICE_START_PTR(response_slice),
                                   GRPC_SLICE_LENGTH(response_slice));
  bool encode_status = pb_encode(
      &ostream, grpc_health_v1_HealthCheckResponse_fields, &response_struct);
  if (!encode_status) {
    return Status(StatusCode::INTERNAL, "Failed to encode response.");
  }
  Slice encoded_response(response_slice, Slice::STEAL_REF);
  ByteBuffer response_buffer(&encoded_response, 1);
  response->Swap(&response_buffer);
  return Status::OK;
}

DefaultHealthCheckService::DefaultHealthCheckService() {
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
DefaultHealthCheckService::GetServingStatus(
    const grpc::string& service_name) const {
  std::lock_guard<std::mutex> lock(mu_);
  const auto& iter = services_map_.find(service_name);
  if (iter == services_map_.end()) {
    return NOT_FOUND;
  }
  return iter->second ? SERVING : NOT_SERVING;
}

DefaultHealthCheckService::HealthCheckServiceImpl*
DefaultHealthCheckService::GetHealthCheckService() {
  GPR_ASSERT(impl_ == nullptr);
  impl_.reset(new HealthCheckServiceImpl(this));
  return impl_.get();
}

}  // namespace grpc
