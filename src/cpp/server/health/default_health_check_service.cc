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

#include "src/cpp/server/health/default_health_check_service.h"

#include <memory>

#include "absl/memory/memory.h"
#include "upb/upb.hpp"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/method_handler.h>

#include "src/proto/grpc/health/v1/health.upb.h"

#define MAX_SERVICE_NAME_LENGTH 200

namespace grpc {

//
// DefaultHealthCheckService
//

DefaultHealthCheckService::DefaultHealthCheckService() {
  services_map_[""].SetServingStatus(SERVING);
}

void DefaultHealthCheckService::SetServingStatus(
    const std::string& service_name, bool serving) {
  grpc::internal::MutexLock lock(&mu_);
  if (shutdown_) {
    // Set to NOT_SERVING in case service_name is not in the map.
    serving = false;
  }
  services_map_[service_name].SetServingStatus(serving ? SERVING : NOT_SERVING);
}

void DefaultHealthCheckService::SetServingStatus(bool serving) {
  const ServingStatus status = serving ? SERVING : NOT_SERVING;
  grpc::internal::MutexLock lock(&mu_);
  if (shutdown_) return;
  for (auto& p : services_map_) {
    ServiceData& service_data = p.second;
    service_data.SetServingStatus(status);
  }
}

void DefaultHealthCheckService::Shutdown() {
  grpc::internal::MutexLock lock(&mu_);
  if (shutdown_) return;
  shutdown_ = true;
  for (auto& p : services_map_) {
    ServiceData& service_data = p.second;
    service_data.SetServingStatus(NOT_SERVING);
  }
}

DefaultHealthCheckService::ServingStatus
DefaultHealthCheckService::GetServingStatus(
    const std::string& service_name) const {
  grpc::internal::MutexLock lock(&mu_);
  auto it = services_map_.find(service_name);
  if (it == services_map_.end()) return NOT_FOUND;
  const ServiceData& service_data = it->second;
  return service_data.GetServingStatus();
}

void DefaultHealthCheckService::RegisterWatch(
    const std::string& service_name,
    grpc_core::RefCountedPtr<HealthCheckServiceImpl::WatchReactor> watcher) {
  grpc::internal::MutexLock lock(&mu_);
  ServiceData& service_data = services_map_[service_name];
  watcher->SendHealth(service_data.GetServingStatus());
  service_data.AddWatch(std::move(watcher));
}

void DefaultHealthCheckService::UnregisterWatch(
    const std::string& service_name,
    HealthCheckServiceImpl::WatchReactor* watcher) {
  grpc::internal::MutexLock lock(&mu_);
  auto it = services_map_.find(service_name);
  if (it == services_map_.end()) return;
  ServiceData& service_data = it->second;
  service_data.RemoveWatch(watcher);
  if (service_data.Unused()) services_map_.erase(it);
}

DefaultHealthCheckService::HealthCheckServiceImpl*
DefaultHealthCheckService::GetHealthCheckService() {
  GPR_ASSERT(impl_ == nullptr);
  impl_ = absl::make_unique<HealthCheckServiceImpl>(this);
  return impl_.get();
}

//
// DefaultHealthCheckService::ServiceData
//

void DefaultHealthCheckService::ServiceData::SetServingStatus(
    ServingStatus status) {
  status_ = status;
  for (const auto& p : watchers_) {
    p.first->SendHealth(status);
  }
}

void DefaultHealthCheckService::ServiceData::AddWatch(
    grpc_core::RefCountedPtr<HealthCheckServiceImpl::WatchReactor> watcher) {
  watchers_[watcher.get()] = std::move(watcher);
}

void DefaultHealthCheckService::ServiceData::RemoveWatch(
    HealthCheckServiceImpl::WatchReactor* watcher) {
  watchers_.erase(watcher);
}

//
// DefaultHealthCheckService::HealthCheckServiceImpl
//

namespace {
const char kHealthCheckMethodName[] = "/grpc.health.v1.Health/Check";
const char kHealthWatchMethodName[] = "/grpc.health.v1.Health/Watch";
}  // namespace

DefaultHealthCheckService::HealthCheckServiceImpl::HealthCheckServiceImpl(
    DefaultHealthCheckService* database)
    : database_(database) {
  // Add Check() method.
  AddMethod(new internal::RpcServiceMethod(
      kHealthCheckMethodName, internal::RpcMethod::NORMAL_RPC, nullptr));
  MarkMethodCallback(
      0, new internal::CallbackUnaryHandler<ByteBuffer, ByteBuffer>(
             [database](CallbackServerContext* context,
                        const ByteBuffer* request, ByteBuffer* response) {
               return HandleCheckRequest(database, context, request, response);
             }));
  // Add Watch() method.
  AddMethod(new internal::RpcServiceMethod(
      kHealthWatchMethodName, internal::RpcMethod::SERVER_STREAMING, nullptr));
  MarkMethodCallback(
      1, new internal::CallbackServerStreamingHandler<ByteBuffer, ByteBuffer>(
             [this](CallbackServerContext* /*ctx*/, const ByteBuffer* request) {
               return new WatchReactor(this, request);
             }));
}

DefaultHealthCheckService::HealthCheckServiceImpl::~HealthCheckServiceImpl() {
  grpc::internal::MutexLock lock(&mu_);
  shutdown_ = true;
  while (num_watches_ > 0) {
    shutdown_condition_.Wait(&mu_);
  }
}

ServerUnaryReactor*
DefaultHealthCheckService::HealthCheckServiceImpl::HandleCheckRequest(
    DefaultHealthCheckService* database, CallbackServerContext* context,
    const ByteBuffer* request, ByteBuffer* response) {
  auto* reactor = context->DefaultReactor();
  std::string service_name;
  if (!DecodeRequest(*request, &service_name)) {
    reactor->Finish(
        Status(StatusCode::INVALID_ARGUMENT, "could not parse request"));
    return reactor;
  }
  ServingStatus serving_status = database->GetServingStatus(service_name);
  if (serving_status == NOT_FOUND) {
    reactor->Finish(Status(StatusCode::NOT_FOUND, "service name unknown"));
    return reactor;
  }
  if (!EncodeResponse(serving_status, response)) {
    reactor->Finish(Status(StatusCode::INTERNAL, "could not encode response"));
    return reactor;
  }
  reactor->Finish(Status::OK);
  return reactor;
}

bool DefaultHealthCheckService::HealthCheckServiceImpl::DecodeRequest(
    const ByteBuffer& request, std::string* service_name) {
  Slice slice;
  if (!request.DumpToSingleSlice(&slice).ok()) return false;
  uint8_t* request_bytes = nullptr;
  size_t request_size = 0;
  request_bytes = const_cast<uint8_t*>(slice.begin());
  request_size = slice.size();
  upb::Arena arena;
  grpc_health_v1_HealthCheckRequest* request_struct =
      grpc_health_v1_HealthCheckRequest_parse(
          reinterpret_cast<char*>(request_bytes), request_size, arena.ptr());
  if (request_struct == nullptr) {
    return false;
  }
  upb_StringView service =
      grpc_health_v1_HealthCheckRequest_service(request_struct);
  if (service.size > MAX_SERVICE_NAME_LENGTH) {
    return false;
  }
  service_name->assign(service.data, service.size);
  return true;
}

bool DefaultHealthCheckService::HealthCheckServiceImpl::EncodeResponse(
    ServingStatus status, ByteBuffer* response) {
  upb::Arena arena;
  grpc_health_v1_HealthCheckResponse* response_struct =
      grpc_health_v1_HealthCheckResponse_new(arena.ptr());
  grpc_health_v1_HealthCheckResponse_set_status(
      response_struct,
      status == NOT_FOUND ? grpc_health_v1_HealthCheckResponse_SERVICE_UNKNOWN
      : status == SERVING ? grpc_health_v1_HealthCheckResponse_SERVING
                          : grpc_health_v1_HealthCheckResponse_NOT_SERVING);
  size_t buf_length;
  char* buf = grpc_health_v1_HealthCheckResponse_serialize(
      response_struct, arena.ptr(), &buf_length);
  if (buf == nullptr) {
    return false;
  }
  grpc_slice response_slice = grpc_slice_from_copied_buffer(buf, buf_length);
  Slice encoded_response(response_slice, Slice::STEAL_REF);
  ByteBuffer response_buffer(&encoded_response, 1);
  response->Swap(&response_buffer);
  return true;
}

//
// DefaultHealthCheckService::HealthCheckServiceImpl::WatchReactor
//

DefaultHealthCheckService::HealthCheckServiceImpl::WatchReactor::WatchReactor(
    HealthCheckServiceImpl* service, const ByteBuffer* request)
    : service_(service) {
  {
    grpc::internal::MutexLock lock(&service_->mu_);
    ++service_->num_watches_;
  }
  if (!DecodeRequest(*request, &service_name_)) {
    MaybeFinish(Status(StatusCode::INTERNAL, "could not parse request"));
    return;
  }
  // Register the call for updates to the service.
  gpr_log(GPR_DEBUG,
          "[HCS %p] Health watch started for service \"%s\" (reactor: %p)",
          service_, service_name_.c_str(), this);
  service_->database_->RegisterWatch(service_name_, Ref());
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchReactor::
    SendHealth(ServingStatus status) {
  grpc::internal::MutexLock lock(&mu_);
  // If there's already a send in flight, cache the new status, and
  // we'll start a new send for it when the one in flight completes.
  if (write_pending_) {
    pending_status_ = status;
    return;
  }
  // Start a send.
  SendHealthLocked(status);
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchReactor::
    SendHealthLocked(ServingStatus status) {
  // Check if we're shutting down.
  {
    grpc::internal::MutexLock lock(&service_->mu_);
    if (service_->shutdown_) {
      MaybeFinish(Status::CANCELLED);
      return;
    }
  }
  // Send response.
  bool success = EncodeResponse(status, &response_);
  if (!success) {
    MaybeFinish(Status(StatusCode::INTERNAL, "could not encode response"));
    return;
  }
  write_pending_ = true;
  StartWrite(&response_);
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchReactor::
    OnWriteDone(bool ok) {
  response_.Clear();
  if (!ok) {
    MaybeFinish(Status::CANCELLED);
    return;
  }
  grpc::internal::MutexLock lock(&mu_);
  write_pending_ = false;
  // If we got a new status since we started the last send, start a
  // new send for it.
  if (pending_status_ != NOT_FOUND) {
    auto status = pending_status_;
    pending_status_ = NOT_FOUND;
    SendHealthLocked(status);
  }
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchReactor::
    OnCancel() {
  MaybeFinish(Status(StatusCode::UNKNOWN, "call cancelled by client"));
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchReactor::OnDone() {
  gpr_log(GPR_DEBUG,
          "[HCS %p] Health watch call finished (service_name: \"%s\", "
          "watcher: %p).",
          service_, service_name_.c_str(), this);
  service_->database_->UnregisterWatch(service_name_, this);
  {
    grpc::internal::MutexLock lock(&service_->mu_);
    if (--service_->num_watches_ == 0 && service_->shutdown_) {
      service_->shutdown_condition_.Signal();
    }
  }
  // Free the initial ref from instantiation.
  Unref();
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchReactor::
    MaybeFinish(Status status) {
  if (!finish_called_.exchange(true)) {
    gpr_log(GPR_DEBUG,
            "[HCS %p] Health watch call finishing with status {code=%d msg=%s} "
            "(service_name: \"%s\", watcher: %p).",
            service_, status.error_code(), status.error_message().c_str(),
            service_name_.c_str(), this);
    Finish(status);
  }
}

}  // namespace grpc
