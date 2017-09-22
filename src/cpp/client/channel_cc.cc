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

#include <grpc++/channel.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include <grpc++/client_context.h>
#include <grpc++/completion_queue.h>
#include <grpc++/impl/call.h>
#include <grpc++/impl/codegen/completion_queue_tag.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc++/impl/rpc_method.h>
#include <grpc++/security/credentials.h>
#include <grpc++/support/channel_arguments.h>
#include <grpc++/support/config.h>
#include <grpc++/support/status.h>
#include <grpc++/support/time.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"

namespace grpc {

namespace {
int kConnectivityCheckIntervalMsec = 500;
void WatchStateChange(void* arg);

class TagSaver final : public CompletionQueueTag {
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

// Constantly watches channel connectivity status to reconnect a transiently
// disconnected channel. This is a temporary work-around before we have retry
// support.
class ChannelConnectivityWatcher : private GrpcLibraryCodegen {
 public:
  static void StartWatching(grpc_channel* channel) {
    if (!IsDisabled()) {
      std::unique_lock<std::mutex> lock(g_watcher_mu_);
      if (g_watcher_ == nullptr) {
        g_watcher_ = new ChannelConnectivityWatcher();
      }
      g_watcher_->StartWatchingLocked(channel);
    }
  }

  static void StopWatching() {
    if (!IsDisabled()) {
      std::unique_lock<std::mutex> lock(g_watcher_mu_);
      if (g_watcher_->StopWatchingLocked()) {
        delete g_watcher_;
        g_watcher_ = nullptr;
      }
    }
  }

 private:
  ChannelConnectivityWatcher() : channel_count_(0), shutdown_(false) {
    gpr_ref_init(&ref_, 0);
    gpr_thd_options options = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&options);
    gpr_thd_new(&thd_id_, &WatchStateChange, this, &options);
  }

  static bool IsDisabled() {
    char* env = gpr_getenv("GRPC_DISABLE_CHANNEL_CONNECTIVITY_WATCHER");
    bool disabled = gpr_is_true(env);
    gpr_free(env);
    return disabled;
  }

  void WatchStateChangeImpl() {
    bool ok = false;
    void* tag = NULL;
    CompletionQueue::NextStatus status = CompletionQueue::GOT_EVENT;
    while (true) {
      {
        std::unique_lock<std::mutex> lock(shutdown_mu_);
        if (shutdown_) {
          // Drain cq_ if the watcher is shutting down
          status = cq_.AsyncNext(&tag, &ok, gpr_inf_future(GPR_CLOCK_REALTIME));
        } else {
          status = cq_.AsyncNext(&tag, &ok, gpr_inf_past(GPR_CLOCK_REALTIME));
          // Make sure we've seen 2 TIMEOUTs before going to sleep
          if (status == CompletionQueue::TIMEOUT) {
            status = cq_.AsyncNext(&tag, &ok, gpr_inf_past(GPR_CLOCK_REALTIME));
            if (status == CompletionQueue::TIMEOUT) {
              shutdown_cv_.wait_for(lock, std::chrono::milliseconds(
                                              kConnectivityCheckIntervalMsec));
              continue;
            }
          }
        }
      }
      ChannelState* channel_state = static_cast<ChannelState*>(tag);
      channel_state->state =
          grpc_channel_check_connectivity_state(channel_state->channel, false);
      if (channel_state->state == GRPC_CHANNEL_SHUTDOWN) {
        void* shutdown_tag = NULL;
        channel_state->shutdown_cq.Next(&shutdown_tag, &ok);
        delete channel_state;
        if (gpr_unref(&ref_)) {
          break;
        }
      } else {
        TagSaver* tag_saver = new TagSaver(channel_state);
        grpc_channel_watch_connectivity_state(
            channel_state->channel, channel_state->state,
            gpr_inf_future(GPR_CLOCK_REALTIME), cq_.cq(), tag_saver);
      }
    }
  }

  void StartWatchingLocked(grpc_channel* channel) {
    if (thd_id_ != 0) {
      gpr_ref(&ref_);
      ++channel_count_;
      ChannelState* channel_state = new ChannelState(channel);
      // The first grpc_channel_watch_connectivity_state() is not used to
      // monitor the channel state change, but to hold a reference of the
      // c channel. So that WatchStateChangeImpl() can observe state ==
      // GRPC_CHANNEL_SHUTDOWN before the channel gets destroyed.
      grpc_channel_watch_connectivity_state(
          channel_state->channel, channel_state->state,
          gpr_inf_future(GPR_CLOCK_REALTIME), channel_state->shutdown_cq.cq(),
          new TagSaver(nullptr));
      grpc_channel_watch_connectivity_state(
          channel_state->channel, channel_state->state,
          gpr_inf_future(GPR_CLOCK_REALTIME), cq_.cq(),
          new TagSaver(channel_state));
    }
  }

  bool StopWatchingLocked() {
    if (--channel_count_ == 0) {
      {
        std::unique_lock<std::mutex> lock(shutdown_mu_);
        shutdown_ = true;
        shutdown_cv_.notify_one();
      }
      gpr_thd_join(thd_id_);
      return true;
    }
    return false;
  }

  friend void WatchStateChange(void* arg);
  struct ChannelState {
    explicit ChannelState(grpc_channel* channel)
        : channel(channel), state(GRPC_CHANNEL_IDLE){};
    grpc_channel* channel;
    grpc_connectivity_state state;
    CompletionQueue shutdown_cq;
  };
  gpr_thd_id thd_id_;
  CompletionQueue cq_;
  gpr_refcount ref_;
  int channel_count_;

  std::mutex shutdown_mu_;
  std::condition_variable shutdown_cv_;  // protected by shutdown_mu_
  bool shutdown_;                        // protected by shutdown_mu_

  static std::mutex g_watcher_mu_;
  static ChannelConnectivityWatcher* g_watcher_;  // protected by g_watcher_mu_
};

std::mutex ChannelConnectivityWatcher::g_watcher_mu_;
ChannelConnectivityWatcher* ChannelConnectivityWatcher::g_watcher_ = nullptr;

void WatchStateChange(void* arg) {
  ChannelConnectivityWatcher* watcher =
      static_cast<ChannelConnectivityWatcher*>(arg);
  watcher->WatchStateChangeImpl();
}
}  // namespace

static internal::GrpcLibraryInitializer g_gli_initializer;
Channel::Channel(const grpc::string& host, grpc_channel* channel)
    : host_(host), c_channel_(channel) {
  g_gli_initializer.summon();
  if (grpc_channel_support_connectivity_watcher(channel)) {
    ChannelConnectivityWatcher::StartWatching(channel);
  }
}

Channel::~Channel() {
  const bool stop_watching =
      grpc_channel_support_connectivity_watcher(c_channel_);
  grpc_channel_destroy(c_channel_);
  if (stop_watching) {
    ChannelConnectivityWatcher::StopWatching();
  }
}

namespace {

grpc::string GetChannelInfoField(grpc_channel* channel,
                                 grpc_channel_info* channel_info,
                                 char*** channel_info_field) {
  char* value = NULL;
  memset(channel_info, 0, sizeof(*channel_info));
  *channel_info_field = &value;
  grpc_channel_get_info(channel, channel_info);
  if (value == NULL) return "";
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

Call Channel::CreateCall(const RpcMethod& method, ClientContext* context,
                         CompletionQueue* cq) {
  const bool kRegistered = method.channel_tag() && context->authority().empty();
  grpc_call* c_call = NULL;
  if (kRegistered) {
    c_call = grpc_channel_create_registered_call(
        c_channel_, context->propagate_from_call_,
        context->propagation_options_.c_bitmask(), cq->cq(),
        method.channel_tag(), context->raw_deadline(), nullptr);
  } else {
    const char* host_str = NULL;
    if (!context->authority().empty()) {
      host_str = context->authority_.c_str();
    } else if (!host_.empty()) {
      host_str = host_.c_str();
    }
    grpc_slice method_slice = SliceFromCopiedString(method.name());
    grpc_slice host_slice;
    if (host_str != nullptr) {
      host_slice = SliceFromCopiedString(host_str);
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
  context->set_call(c_call, shared_from_this());
  return Call(c_call, this, cq);
}

void Channel::PerformOpsOnCall(CallOpSetInterface* ops, Call* call) {
  static const size_t MAX_OPS = 8;
  size_t nops = 0;
  grpc_op cops[MAX_OPS];
  ops->FillOps(call->call(), cops, &nops);
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_batch(call->call(), cops, nops, ops, nullptr));
}

void* Channel::RegisterMethod(const char* method) {
  return grpc_channel_register_call(
      c_channel_, method, host_.empty() ? NULL : host_.c_str(), nullptr);
}

grpc_connectivity_state Channel::GetState(bool try_to_connect) {
  return grpc_channel_check_connectivity_state(c_channel_, try_to_connect);
}

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
  void* tag = NULL;
  NotifyOnStateChangeImpl(last_observed, deadline, &cq, NULL);
  cq.Next(&tag, &ok);
  GPR_ASSERT(tag == NULL);
  return ok;
}

}  // namespace grpc
