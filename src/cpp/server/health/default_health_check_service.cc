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

#include <memory>
#include <mutex>

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/method_handler_impl.h>

#include "pb_decode.h"
#include "pb_encode.h"
#include "src/core/ext/filters/client_channel/health/health.pb.h"
#include "src/cpp/server/health/default_health_check_service.h"

namespace grpc {

//
// DefaultHealthCheckService
//

DefaultHealthCheckService::DefaultHealthCheckService() {
  services_map_[""].SetServingStatus(SERVING);
}

void DefaultHealthCheckService::SetServingStatus(
    const grpc::string& service_name, bool serving) {
  std::unique_lock<std::mutex> lock(mu_);
  if (shutdown_) {
    // Set to NOT_SERVING in case service_name is not in the map.
    serving = false;
  }
  services_map_[service_name].SetServingStatus(serving ? SERVING : NOT_SERVING);
}

void DefaultHealthCheckService::SetServingStatus(bool serving) {
  const ServingStatus status = serving ? SERVING : NOT_SERVING;
  std::unique_lock<std::mutex> lock(mu_);
  if (shutdown_) {
    return;
  }
  for (auto& p : services_map_) {
    ServiceData& service_data = p.second;
    service_data.SetServingStatus(status);
  }
}

void DefaultHealthCheckService::Shutdown() {
  std::unique_lock<std::mutex> lock(mu_);
  if (shutdown_) {
    return;
  }
  shutdown_ = true;
  for (auto& p : services_map_) {
    ServiceData& service_data = p.second;
    service_data.SetServingStatus(NOT_SERVING);
  }
}

DefaultHealthCheckService::ServingStatus
DefaultHealthCheckService::GetServingStatus(
    const grpc::string& service_name) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = services_map_.find(service_name);
  if (it == services_map_.end()) {
    return NOT_FOUND;
  }
  const ServiceData& service_data = it->second;
  return service_data.GetServingStatus();
}

void DefaultHealthCheckService::RegisterCallHandler(
    const grpc::string& service_name,
    std::shared_ptr<HealthCheckServiceImpl::CallHandler> handler) {
  std::unique_lock<std::mutex> lock(mu_);
  ServiceData& service_data = services_map_[service_name];
  service_data.AddCallHandler(handler /* copies ref */);
  HealthCheckServiceImpl::CallHandler* h = handler.get();
  h->SendHealth(std::move(handler), service_data.GetServingStatus());
}

void DefaultHealthCheckService::UnregisterCallHandler(
    const grpc::string& service_name,
    const std::shared_ptr<HealthCheckServiceImpl::CallHandler>& handler) {
  std::unique_lock<std::mutex> lock(mu_);
  auto it = services_map_.find(service_name);
  if (it == services_map_.end()) return;
  ServiceData& service_data = it->second;
  service_data.RemoveCallHandler(handler);
  if (service_data.Unused()) {
    services_map_.erase(it);
  }
}

DefaultHealthCheckService::HealthCheckServiceImpl*
DefaultHealthCheckService::GetHealthCheckService(
    std::unique_ptr<ServerCompletionQueue> cq) {
  GPR_ASSERT(impl_ == nullptr);
  impl_.reset(new HealthCheckServiceImpl(this, std::move(cq)));
  return impl_.get();
}

//
// DefaultHealthCheckService::ServiceData
//

void DefaultHealthCheckService::ServiceData::SetServingStatus(
    ServingStatus status) {
  status_ = status;
  for (auto& call_handler : call_handlers_) {
    call_handler->SendHealth(call_handler /* copies ref */, status);
  }
}

void DefaultHealthCheckService::ServiceData::AddCallHandler(
    std::shared_ptr<HealthCheckServiceImpl::CallHandler> handler) {
  call_handlers_.insert(std::move(handler));
}

void DefaultHealthCheckService::ServiceData::RemoveCallHandler(
    const std::shared_ptr<HealthCheckServiceImpl::CallHandler>& handler) {
  call_handlers_.erase(handler);
}

//
// DefaultHealthCheckService::HealthCheckServiceImpl
//

namespace {
const char kHealthCheckMethodName[] = "/grpc.health.v1.Health/Check";
const char kHealthWatchMethodName[] = "/grpc.health.v1.Health/Watch";
}  // namespace

DefaultHealthCheckService::HealthCheckServiceImpl::HealthCheckServiceImpl(
    DefaultHealthCheckService* database,
    std::unique_ptr<ServerCompletionQueue> cq)
    : database_(database), cq_(std::move(cq)) {
  // Add Check() method.
  AddMethod(new internal::RpcServiceMethod(
      kHealthCheckMethodName, internal::RpcMethod::NORMAL_RPC, nullptr));
  // Add Watch() method.
  AddMethod(new internal::RpcServiceMethod(
      kHealthWatchMethodName, internal::RpcMethod::SERVER_STREAMING, nullptr));
  // Create serving thread.
  thread_ = std::unique_ptr<::grpc_core::Thread>(
      new ::grpc_core::Thread("grpc_health_check_service", Serve, this));
}

DefaultHealthCheckService::HealthCheckServiceImpl::~HealthCheckServiceImpl() {
  // We will reach here after the server starts shutting down.
  shutdown_ = true;
  {
    std::unique_lock<std::mutex> lock(cq_shutdown_mu_);
    cq_->Shutdown();
  }
  thread_->Join();
}

void DefaultHealthCheckService::HealthCheckServiceImpl::StartServingThread() {
  // Request the calls we're interested in.
  // We do this before starting the serving thread, so that we know it's
  // done before server startup is complete.
  CheckCallHandler::CreateAndStart(cq_.get(), database_, this);
  WatchCallHandler::CreateAndStart(cq_.get(), database_, this);
  // Start serving thread.
  thread_->Start();
}

void DefaultHealthCheckService::HealthCheckServiceImpl::Serve(void* arg) {
  HealthCheckServiceImpl* service =
      reinterpret_cast<HealthCheckServiceImpl*>(arg);
  void* tag;
  bool ok;
  while (true) {
    if (!service->cq_->Next(&tag, &ok)) {
      // The completion queue is shutting down.
      GPR_ASSERT(service->shutdown_);
      break;
    }
    auto* next_step = static_cast<CallableTag*>(tag);
    next_step->Run(ok);
  }
}

bool DefaultHealthCheckService::HealthCheckServiceImpl::DecodeRequest(
    const ByteBuffer& request, grpc::string* service_name) {
  std::vector<Slice> slices;
  if (!request.Dump(&slices).ok()) return false;
  uint8_t* request_bytes = nullptr;
  size_t request_size = 0;
  grpc_health_v1_HealthCheckRequest request_struct;
  request_struct.has_service = false;
  if (slices.size() == 1) {
    request_bytes = const_cast<uint8_t*>(slices[0].begin());
    request_size = slices[0].size();
  } else if (slices.size() > 1) {
    request_bytes = static_cast<uint8_t*>(gpr_malloc(request.Length()));
    uint8_t* copy_to = request_bytes;
    for (size_t i = 0; i < slices.size(); i++) {
      memcpy(copy_to, slices[i].begin(), slices[i].size());
      copy_to += slices[i].size();
    }
  }
  pb_istream_t istream = pb_istream_from_buffer(request_bytes, request_size);
  bool decode_status = pb_decode(
      &istream, grpc_health_v1_HealthCheckRequest_fields, &request_struct);
  if (slices.size() > 1) {
    gpr_free(request_bytes);
  }
  if (!decode_status) return false;
  *service_name = request_struct.has_service ? request_struct.service : "";
  return true;
}

bool DefaultHealthCheckService::HealthCheckServiceImpl::EncodeResponse(
    ServingStatus status, ByteBuffer* response) {
  grpc_health_v1_HealthCheckResponse response_struct;
  response_struct.has_status = true;
  response_struct.status =
      status == NOT_FOUND
          ? grpc_health_v1_HealthCheckResponse_ServingStatus_SERVICE_UNKNOWN
          : status == SERVING
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
  if (!encode_status) return false;
  Slice encoded_response(response_slice, Slice::STEAL_REF);
  ByteBuffer response_buffer(&encoded_response, 1);
  response->Swap(&response_buffer);
  return true;
}

//
// DefaultHealthCheckService::HealthCheckServiceImpl::CheckCallHandler
//

void DefaultHealthCheckService::HealthCheckServiceImpl::CheckCallHandler::
    CreateAndStart(ServerCompletionQueue* cq,
                   DefaultHealthCheckService* database,
                   HealthCheckServiceImpl* service) {
  std::shared_ptr<CallHandler> self =
      std::make_shared<CheckCallHandler>(cq, database, service);
  CheckCallHandler* handler = static_cast<CheckCallHandler*>(self.get());
  {
    std::unique_lock<std::mutex> lock(service->cq_shutdown_mu_);
    if (service->shutdown_) return;
    // Request a Check() call.
    handler->next_ =
        CallableTag(std::bind(&CheckCallHandler::OnCallReceived, handler,
                              std::placeholders::_1, std::placeholders::_2),
                    std::move(self));
    service->RequestAsyncUnary(0, &handler->ctx_, &handler->request_,
                               &handler->writer_, cq, cq, &handler->next_);
  }
}

DefaultHealthCheckService::HealthCheckServiceImpl::CheckCallHandler::
    CheckCallHandler(ServerCompletionQueue* cq,
                     DefaultHealthCheckService* database,
                     HealthCheckServiceImpl* service)
    : cq_(cq), database_(database), service_(service), writer_(&ctx_) {}

void DefaultHealthCheckService::HealthCheckServiceImpl::CheckCallHandler::
    OnCallReceived(std::shared_ptr<CallHandler> self, bool ok) {
  if (!ok) {
    // The value of ok being false means that the server is shutting down.
    return;
  }
  // Spawn a new handler instance to serve the next new client. Every handler
  // instance will deallocate itself when it's done.
  CreateAndStart(cq_, database_, service_);
  // Process request.
  gpr_log(GPR_DEBUG, "[HCS %p] Health check started for handler %p", service_,
          this);
  grpc::string service_name;
  grpc::Status status = Status::OK;
  ByteBuffer response;
  if (!service_->DecodeRequest(request_, &service_name)) {
    status = Status(StatusCode::INVALID_ARGUMENT, "could not parse request");
  } else {
    ServingStatus serving_status = database_->GetServingStatus(service_name);
    if (serving_status == NOT_FOUND) {
      status = Status(StatusCode::NOT_FOUND, "service name unknown");
    } else if (!service_->EncodeResponse(serving_status, &response)) {
      status = Status(StatusCode::INTERNAL, "could not encode response");
    }
  }
  // Send response.
  {
    std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
    if (!service_->shutdown_) {
      next_ =
          CallableTag(std::bind(&CheckCallHandler::OnFinishDone, this,
                                std::placeholders::_1, std::placeholders::_2),
                      std::move(self));
      if (status.ok()) {
        writer_.Finish(response, status, &next_);
      } else {
        writer_.FinishWithError(status, &next_);
      }
    }
  }
}

void DefaultHealthCheckService::HealthCheckServiceImpl::CheckCallHandler::
    OnFinishDone(std::shared_ptr<CallHandler> self, bool ok) {
  if (ok) {
    gpr_log(GPR_DEBUG, "[HCS %p] Health check call finished for handler %p",
            service_, this);
  }
  self.reset();  // To appease clang-tidy.
}

//
// DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler
//

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    CreateAndStart(ServerCompletionQueue* cq,
                   DefaultHealthCheckService* database,
                   HealthCheckServiceImpl* service) {
  std::shared_ptr<CallHandler> self =
      std::make_shared<WatchCallHandler>(cq, database, service);
  WatchCallHandler* handler = static_cast<WatchCallHandler*>(self.get());
  {
    std::unique_lock<std::mutex> lock(service->cq_shutdown_mu_);
    if (service->shutdown_) return;
    // Request AsyncNotifyWhenDone().
    handler->on_done_notified_ =
        CallableTag(std::bind(&WatchCallHandler::OnDoneNotified, handler,
                              std::placeholders::_1, std::placeholders::_2),
                    self /* copies ref */);
    handler->ctx_.AsyncNotifyWhenDone(&handler->on_done_notified_);
    // Request a Watch() call.
    handler->next_ =
        CallableTag(std::bind(&WatchCallHandler::OnCallReceived, handler,
                              std::placeholders::_1, std::placeholders::_2),
                    std::move(self));
    service->RequestAsyncServerStreaming(1, &handler->ctx_, &handler->request_,
                                         &handler->stream_, cq, cq,
                                         &handler->next_);
  }
}

DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    WatchCallHandler(ServerCompletionQueue* cq,
                     DefaultHealthCheckService* database,
                     HealthCheckServiceImpl* service)
    : cq_(cq), database_(database), service_(service), stream_(&ctx_) {}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    OnCallReceived(std::shared_ptr<CallHandler> self, bool ok) {
  if (!ok) {
    // Server shutting down.
    //
    // AsyncNotifyWhenDone() needs to be called before the call starts, but the
    // tag will not pop out if the call never starts (
    // https://github.com/grpc/grpc/issues/10136). So we need to manually
    // release the ownership of the handler in this case.
    GPR_ASSERT(on_done_notified_.ReleaseHandler() != nullptr);
    return;
  }
  // Spawn a new handler instance to serve the next new client. Every handler
  // instance will deallocate itself when it's done.
  CreateAndStart(cq_, database_, service_);
  // Parse request.
  if (!service_->DecodeRequest(request_, &service_name_)) {
    SendFinish(std::move(self),
               Status(StatusCode::INVALID_ARGUMENT, "could not parse request"));
    return;
  }
  // Register the call for updates to the service.
  gpr_log(GPR_DEBUG,
          "[HCS %p] Health watch started for service \"%s\" (handler: %p)",
          service_, service_name_.c_str(), this);
  database_->RegisterCallHandler(service_name_, std::move(self));
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    SendHealth(std::shared_ptr<CallHandler> self, ServingStatus status) {
  std::unique_lock<std::mutex> lock(send_mu_);
  // If there's already a send in flight, cache the new status, and
  // we'll start a new send for it when the one in flight completes.
  if (send_in_flight_) {
    pending_status_ = status;
    return;
  }
  // Start a send.
  SendHealthLocked(std::move(self), status);
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    SendHealthLocked(std::shared_ptr<CallHandler> self, ServingStatus status) {
  send_in_flight_ = true;
  // Construct response.
  ByteBuffer response;
  bool success = service_->EncodeResponse(status, &response);
  // Grab shutdown lock and send response.
  std::unique_lock<std::mutex> cq_lock(service_->cq_shutdown_mu_);
  if (service_->shutdown_) {
    SendFinishLocked(std::move(self), Status::CANCELLED);
    return;
  }
  if (!success) {
    SendFinishLocked(std::move(self),
                     Status(StatusCode::INTERNAL, "could not encode response"));
    return;
  }
  next_ = CallableTag(std::bind(&WatchCallHandler::OnSendHealthDone, this,
                                std::placeholders::_1, std::placeholders::_2),
                      std::move(self));
  stream_.Write(response, &next_);
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    OnSendHealthDone(std::shared_ptr<CallHandler> self, bool ok) {
  if (!ok) {
    SendFinish(std::move(self), Status::CANCELLED);
    return;
  }
  std::unique_lock<std::mutex> lock(send_mu_);
  send_in_flight_ = false;
  // If we got a new status since we started the last send, start a
  // new send for it.
  if (pending_status_ != NOT_FOUND) {
    auto status = pending_status_;
    pending_status_ = NOT_FOUND;
    SendHealthLocked(std::move(self), status);
  }
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    SendFinish(std::shared_ptr<CallHandler> self, const Status& status) {
  if (finish_called_) return;
  std::unique_lock<std::mutex> cq_lock(service_->cq_shutdown_mu_);
  if (service_->shutdown_) return;
  SendFinishLocked(std::move(self), status);
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    SendFinishLocked(std::shared_ptr<CallHandler> self, const Status& status) {
  on_finish_done_ =
      CallableTag(std::bind(&WatchCallHandler::OnFinishDone, this,
                            std::placeholders::_1, std::placeholders::_2),
                  std::move(self));
  stream_.Finish(status, &on_finish_done_);
  finish_called_ = true;
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    OnFinishDone(std::shared_ptr<CallHandler> self, bool ok) {
  if (ok) {
    gpr_log(GPR_DEBUG,
            "[HCS %p] Health watch call finished (service_name: \"%s\", "
            "handler: %p).",
            service_, service_name_.c_str(), this);
  }
  self.reset();  // To appease clang-tidy.
}

// TODO(roth): This method currently assumes that there will be only one
// thread polling the cq and invoking the corresponding callbacks.  If
// that changes, we will need to add synchronization here.
void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    OnDoneNotified(std::shared_ptr<CallHandler> self, bool ok) {
  GPR_ASSERT(ok);
  gpr_log(GPR_DEBUG,
          "[HCS %p] Health watch call is notified done (handler: %p, "
          "is_cancelled: %d).",
          service_, this, static_cast<int>(ctx_.IsCancelled()));
  database_->UnregisterCallHandler(service_name_, self);
  SendFinish(std::move(self), Status::CANCELLED);
}

}  // namespace grpc
