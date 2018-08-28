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
#include "src/cpp/server/health/default_health_check_service.h"
#include "src/cpp/server/health/health.pb.h"

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
  services_map_[service_name].SetServingStatus(serving ? SERVING : NOT_SERVING);
}

void DefaultHealthCheckService::SetServingStatus(bool serving) {
  const ServingStatus status = serving ? SERVING : NOT_SERVING;
  std::unique_lock<std::mutex> lock(mu_);
  for (auto& p : services_map_) {
    ServiceData& service_data = p.second;
    service_data.SetServingStatus(status);
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
  handler->SendHealth(std::move(handler), service_data.GetServingStatus());
}

void DefaultHealthCheckService::UnregisterCallHandler(
    const grpc::string& service_name,
    std::shared_ptr<HealthCheckServiceImpl::CallHandler> handler) {
  std::unique_lock<std::mutex> lock(mu_);
  auto it = services_map_.find(service_name);
  if (it == services_map_.end()) return;
  ServiceData& service_data = it->second;
  service_data.RemoveCallHandler(std::move(handler));
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
    std::shared_ptr<HealthCheckServiceImpl::CallHandler> handler) {
  call_handlers_.erase(std::move(handler));
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
  check_method_ = new internal::RpcServiceMethod(
      kHealthCheckMethodName, internal::RpcMethod::NORMAL_RPC, nullptr);
  AddMethod(check_method_);
  // Add Watch() method.
  watch_method_ = new internal::RpcServiceMethod(
      kHealthWatchMethodName, internal::RpcMethod::SERVER_STREAMING, nullptr);
  AddMethod(watch_method_);
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
  thread_->Start();
}

void DefaultHealthCheckService::HealthCheckServiceImpl::Serve(void* arg) {
  HealthCheckServiceImpl* service =
      reinterpret_cast<HealthCheckServiceImpl*>(arg);
  // TODO(juanlishen): This is a workaround to wait for the cq to be ready.
  // Need to figure out why cq is not ready after service starts.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                               gpr_time_from_seconds(1, GPR_TIMESPAN)));
  CheckCallHandler::CreateAndStart(service->cq_.get(), service->database_,
                                   service);
  WatchCallHandler::CreateAndStart(service->cq_.get(), service->database_,
                                   service);
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
    request_bytes = static_cast<uint8_t*>(gpr_malloc(request.Length()));
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
    if (!decode_status) return false;
  }
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
    status = Status(INVALID_ARGUMENT, "");
  } else {
    ServingStatus serving_status = database_->GetServingStatus(service_name);
    if (serving_status == NOT_FOUND) {
      status = Status(StatusCode::NOT_FOUND, "service name unknown");
    } else if (!service_->EncodeResponse(serving_status, &response)) {
      status = Status(INTERNAL, "");
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
    : cq_(cq),
      database_(database),
      service_(service),
      stream_(&ctx_),
      call_state_(WAITING_FOR_CALL) {}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    OnCallReceived(std::shared_ptr<CallHandler> self, bool ok) {
  if (ok) {
    call_state_ = CALL_RECEIVED;
  } else {
    // AsyncNotifyWhenDone() needs to be called before the call starts, but the
    // tag will not pop out if the call never starts (
    // https://github.com/grpc/grpc/issues/10136). So we need to manually
    // release the ownership of the handler in this case.
    GPR_ASSERT(on_done_notified_.ReleaseHandler() != nullptr);
  }
  if (!ok || shutdown_) {
    // The value of ok being false means that the server is shutting down.
    Shutdown(std::move(self), "OnCallReceived");
    return;
  }
  // Spawn a new handler instance to serve the next new client. Every handler
  // instance will deallocate itself when it's done.
  CreateAndStart(cq_, database_, service_);
  // Parse request.
  if (!service_->DecodeRequest(request_, &service_name_)) {
    on_finish_done_ =
        CallableTag(std::bind(&WatchCallHandler::OnFinishDone, this,
                              std::placeholders::_1, std::placeholders::_2),
                    std::move(self));
    stream_.Finish(Status(INVALID_ARGUMENT, ""), &on_finish_done_);
    call_state_ = FINISH_CALLED;
    return;
  }
  // Register the call for updates to the service.
  gpr_log(GPR_DEBUG,
          "[HCS %p] Health check watch started for service \"%s\" "
          "(handler: %p)",
          service_, service_name_.c_str(), this);
  database_->RegisterCallHandler(service_name_, std::move(self));
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    SendHealth(std::shared_ptr<CallHandler> self, ServingStatus status) {
  std::unique_lock<std::mutex> lock(mu_);
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
  std::unique_lock<std::mutex> cq_lock(service_->cq_shutdown_mu_);
  if (service_->shutdown_) {
    cq_lock.release()->unlock();
    Shutdown(std::move(self), "SendHealthLocked");
    return;
  }
  send_in_flight_ = true;
  call_state_ = SEND_MESSAGE_PENDING;
  // Construct response.
  ByteBuffer response;
  if (!service_->EncodeResponse(status, &response)) {
    on_finish_done_ =
        CallableTag(std::bind(&WatchCallHandler::OnFinishDone, this,
                              std::placeholders::_1, std::placeholders::_2),
                    std::move(self));
    stream_.Finish(Status(INTERNAL, ""), &on_finish_done_);
    return;
  }
  next_ = CallableTag(std::bind(&WatchCallHandler::OnSendHealthDone, this,
                                std::placeholders::_1, std::placeholders::_2),
                      std::move(self));
  stream_.Write(response, &next_);
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    OnSendHealthDone(std::shared_ptr<CallHandler> self, bool ok) {
  if (!ok || shutdown_) {
    Shutdown(std::move(self), "OnSendHealthDone");
    return;
  }
  call_state_ = CALL_RECEIVED;
  {
    std::unique_lock<std::mutex> lock(mu_);
    send_in_flight_ = false;
    // If we got a new status since we started the last send, start a
    // new send for it.
    if (pending_status_ != NOT_FOUND) {
      auto status = pending_status_;
      pending_status_ = NOT_FOUND;
      SendHealthLocked(std::move(self), status);
    }
  }
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    OnDoneNotified(std::shared_ptr<CallHandler> self, bool ok) {
  GPR_ASSERT(ok);
  done_notified_ = true;
  if (ctx_.IsCancelled()) {
    is_cancelled_ = true;
  }
  gpr_log(GPR_DEBUG,
          "[HCS %p] Healt check call is notified done (handler: %p, "
          "is_cancelled: %d).",
          service_, this, static_cast<int>(is_cancelled_));
  Shutdown(std::move(self), "OnDoneNotified");
}

// TODO(roth): This method currently assumes that there will be only one
// thread polling the cq and invoking the corresponding callbacks.  If
// that changes, we will need to add synchronization here.
void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    Shutdown(std::shared_ptr<CallHandler> self, const char* reason) {
  if (!shutdown_) {
    gpr_log(GPR_DEBUG,
            "[HCS %p] Shutting down the handler (service_name: \"%s\", "
            "handler: %p, reason: %s).",
            service_, service_name_.c_str(), this, reason);
    shutdown_ = true;
  }
  // OnCallReceived() may be called after OnDoneNotified(), so we need to
  // try to Finish() every time we are in Shutdown().
  if (call_state_ >= CALL_RECEIVED && call_state_ < FINISH_CALLED) {
    std::unique_lock<std::mutex> lock(service_->cq_shutdown_mu_);
    if (!service_->shutdown_) {
      on_finish_done_ =
          CallableTag(std::bind(&WatchCallHandler::OnFinishDone, this,
                                std::placeholders::_1, std::placeholders::_2),
                      std::move(self));
      // TODO(juanlishen): Maybe add a message proto for the client to
      // explicitly cancel the stream so that we can return OK status in such
      // cases.
      stream_.Finish(Status::CANCELLED, &on_finish_done_);
      call_state_ = FINISH_CALLED;
    }
  }
}

void DefaultHealthCheckService::HealthCheckServiceImpl::WatchCallHandler::
    OnFinishDone(std::shared_ptr<CallHandler> self, bool ok) {
  if (ok) {
    gpr_log(GPR_DEBUG,
            "[HCS %p] Health check call finished (service_name: \"%s\", "
            "handler: %p).",
            service_, service_name_.c_str(), this);
  }
}

}  // namespace grpc
