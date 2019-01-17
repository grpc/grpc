/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpcpp/channel.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpcpp/client_context.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/impl/call.h>
#include <grpcpp/impl/codegen/call_op_set.h>
#include <grpcpp/impl/codegen/completion_queue_tag.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/impl/rpc_method.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/config.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/time.h>
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/surface/completion_queue.h"

namespace grpc {

static internal::GrpcLibraryInitializer g_gli_initializer;
Channel::Channel(
    const grpc::string& host, grpc_channel* channel,
    std::vector<
        std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
        interceptor_creators)
    : host_(host), c_channel_(channel) {
  interceptor_creators_ = std::move(interceptor_creators);
  g_gli_initializer.summon();
}

Channel::~Channel() {
  grpc_channel_destroy(c_channel_);
  if (callback_cq_ != nullptr) {
    callback_cq_->Shutdown();
  }
}

namespace {

inline grpc_slice SliceFromArray(const char* arr, size_t len) {
  return g_core_codegen_interface->grpc_slice_from_copied_buffer(arr, len);
}

grpc::string GetChannelInfoField(grpc_channel* channel,
                                 grpc_channel_info* channel_info,
                                 char*** channel_info_field) {
  char* value = nullptr;
  memset(channel_info, 0, sizeof(*channel_info));
  *channel_info_field = &value;
  grpc_channel_get_info(channel, channel_info);
  if (value == nullptr) return "";
  grpc::string result = value;
  gpr_free(value);
  return result;
}

}  // namespace

grpc::string Channel::GetLoadBalancingPolicyName() const {
  grpc_channel_info channel_info;
  return GetChannelInfoField(c_channel_, &channel_info,
                             &channel_info.lb_policy_name);
}

grpc::string Channel::GetServiceConfigJSON() const {
  grpc_channel_info channel_info;
  return GetChannelInfoField(c_channel_, &channel_info,
                             &channel_info.service_config_json);
}

namespace experimental {

void ChannelResetConnectionBackoff(Channel* channel) {
  grpc_channel_reset_connect_backoff(channel->c_channel_);
}

}  // namespace experimental

internal::Call Channel::CreateCallInternal(const internal::RpcMethod& method,
                                           ClientContext* context,
                                           CompletionQueue* cq,
                                           size_t interceptor_pos) {
  const bool kRegistered = method.channel_tag() && context->authority().empty();
  grpc_call* c_call = nullptr;
  if (kRegistered) {
    c_call = grpc_channel_create_registered_call(
        c_channel_, context->propagate_from_call_,
        context->propagation_options_.c_bitmask(), cq->cq(),
        method.channel_tag(), context->raw_deadline(), nullptr);
  } else {
    const string* host_str = nullptr;
    if (!context->authority_.empty()) {
      host_str = &context->authority_;
    } else if (!host_.empty()) {
      host_str = &host_;
    }
    grpc_slice method_slice =
        SliceFromArray(method.name(), strlen(method.name()));
    grpc_slice host_slice;
    if (host_str != nullptr) {
      host_slice = SliceFromCopiedString(*host_str);
    }
    c_call = grpc_channel_create_call(
        c_channel_, context->propagate_from_call_,
        context->propagation_options_.c_bitmask(), cq->cq(), method_slice,
        host_str == nullptr ? nullptr : &host_slice, context->raw_deadline(),
        nullptr);
    grpc_slice_unref(method_slice);
    if (host_str != nullptr) {
      grpc_slice_unref(host_slice);
    }
  }
  grpc_census_call_set_context(c_call, context->census_context());

  // ClientRpcInfo should be set before call because set_call also checks
  // whether the call has been cancelled, and if the call was cancelled, we
  // should notify the interceptors too/
  auto* info =
      context->set_client_rpc_info(method.name(), method.method_type(), this,
                                   interceptor_creators_, interceptor_pos);
  context->set_call(c_call, shared_from_this());

  return internal::Call(c_call, this, cq, info);
}

internal::Call Channel::CreateCall(const internal::RpcMethod& method,
                                   ClientContext* context,
                                   CompletionQueue* cq) {
  return CreateCallInternal(method, context, cq, 0);
}

void Channel::PerformOpsOnCall(internal::CallOpSetInterface* ops,
                               internal::Call* call) {
  ops->FillOps(
      call);  // Make a copy of call. It's fine since Call just has pointers
}

void* Channel::RegisterMethod(const char* method) {
  return grpc_channel_register_call(
      c_channel_, method, host_.empty() ? nullptr : host_.c_str(), nullptr);
}

grpc_connectivity_state Channel::GetState(bool try_to_connect) {
  return grpc_channel_check_connectivity_state(c_channel_, try_to_connect);
}

namespace {

class TagSaver final : public internal::CompletionQueueTag {
 public:
  explicit TagSaver(void* tag) : tag_(tag) {}
  ~TagSaver() override {}
  bool FinalizeResult(void** tag, bool* status) override {
    *tag = tag_;
    delete this;
    return true;
  }

 private:
  void* tag_;
};

}  // namespace

void Channel::NotifyOnStateChangeImpl(grpc_connectivity_state last_observed,
                                      gpr_timespec deadline,
                                      CompletionQueue* cq, void* tag) {
  TagSaver* tag_saver = new TagSaver(tag);
  grpc_channel_watch_connectivity_state(c_channel_, last_observed, deadline,
                                        cq->cq(), tag_saver);
}

bool Channel::WaitForStateChangeImpl(grpc_connectivity_state last_observed,
                                     gpr_timespec deadline) {
  CompletionQueue cq;
  bool ok = false;
  void* tag = nullptr;
  NotifyOnStateChangeImpl(last_observed, deadline, &cq, nullptr);
  cq.Next(&tag, &ok);
  GPR_ASSERT(tag == nullptr);
  return ok;
}

namespace {
class ShutdownCallback : public grpc_experimental_completion_queue_functor {
 public:
  ShutdownCallback() { functor_run = &ShutdownCallback::Run; }
  // TakeCQ takes ownership of the cq into the shutdown callback
  // so that the shutdown callback will be responsible for destroying it
  void TakeCQ(CompletionQueue* cq) { cq_ = cq; }

  // The Run function will get invoked by the completion queue library
  // when the shutdown is actually complete
  static void Run(grpc_experimental_completion_queue_functor* cb, int) {
    auto* callback = static_cast<ShutdownCallback*>(cb);
    delete callback->cq_;
    delete callback;
  }

 private:
  CompletionQueue* cq_ = nullptr;
};
}  // namespace

CompletionQueue* Channel::CallbackCQ() {
  // TODO(vjpai): Consider using a single global CQ for the default CQ
  // if there is no explicit per-channel CQ registered
  std::lock_guard<std::mutex> l(mu_);
  if (callback_cq_ == nullptr) {
    auto* shutdown_callback = new ShutdownCallback;
    callback_cq_ = new CompletionQueue(grpc_completion_queue_attributes{
        GRPC_CQ_CURRENT_VERSION, GRPC_CQ_CALLBACK, GRPC_CQ_DEFAULT_POLLING,
        shutdown_callback});

    // Transfer ownership of the new cq to its own shutdown callback
    shutdown_callback->TakeCQ(callback_cq_);
  }
  return callback_cq_;
}

}  // namespace grpc
