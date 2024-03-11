//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/legacy_channel.h"

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/channel/metrics.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/init_internally.h"
#include "src/core/lib/surface/lame_client.h"
#include "src/core/lib/transport/call_factory.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

absl::StatusOr<OrphanablePtr<Channel>> LegacyChannel::Create(
    std::string target, ChannelArgs args,
    grpc_channel_stack_type channel_stack_type) {
  if (grpc_channel_stack_type_is_client(channel_stack_type)) {
    auto channel_args_mutator =
        grpc_channel_args_get_client_channel_creation_mutator();
    if (channel_args_mutator != nullptr) {
      args = channel_args_mutator(target.c_str(), args, channel_stack_type);
    }
  }
  ChannelStackBuilderImpl builder(
      grpc_channel_stack_type_string(channel_stack_type), channel_stack_type,
      args);
  builder.SetTarget(target.c_str());
  if (!CoreConfiguration::Get().channel_init().CreateStack(&builder)) {
    return nullptr;
  }
  // Only need to update stats for server channels here.  Stats for client
  // channels are handled in our base class.
  if (builder.channel_stack_type() == GRPC_SERVER_CHANNEL) {
    global_stats().IncrementServerChannelsCreated();
  }
  absl::StatusOr<RefCountedPtr<grpc_channel_stack>> r = builder.Build();
  if (!r.ok()) {
    auto status = r.status();
    gpr_log(GPR_ERROR, "channel stack builder failed: %s",
            status.ToString().c_str());
    return status;
  }
  // TODO(roth): Figure out how to populate authority here.
  // Or maybe just don't worry about this if no one needs it until after
  // the call v3 stack lands.
  StatsPlugin::ChannelScope scope(builder.target(), "");
  *(*r)->stats_plugin_group =
      GlobalStatsPluginRegistry::GetStatsPluginsForChannel(scope);
  return MakeOrphanable<LegacyChannel>(
      grpc_channel_stack_type_is_client(builder.channel_stack_type()),
      builder.IsPromising(), std::move(target), args, std::move(*r));
}

namespace {

class NotReallyACallFactory final : public CallFactory {
 public:
  using CallFactory::CallFactory;
  CallInitiator CreateCall(ClientMetadataHandle, Arena*) override {
    Crash("NotReallyACallFactory::CreateCall should never be called");
  }
};

}  // namespace

LegacyChannel::LegacyChannel(bool is_client, bool is_promising,
                             std::string target,
                             const ChannelArgs& channel_args,
                             RefCountedPtr<grpc_channel_stack> channel_stack)
    : Channel(std::move(target), channel_args),
      is_client_(is_client),
      is_promising_(is_promising),
      channel_stack_(std::move(channel_stack)),
      call_factory_(MakeRefCounted<NotReallyACallFactory>(channel_args)) {
  // We need to make sure that grpc_shutdown() does not shut things down
  // until after the channel is destroyed.  However, the channel may not
  // actually be destroyed by the time grpc_channel_destroy() returns,
  // since there may be other existing refs to the channel.  If those
  // refs are held by things that are visible to the wrapped language
  // (such as outstanding calls on the channel), then the wrapped
  // language can be responsible for making sure that grpc_shutdown()
  // does not run until after those refs are released.  However, the
  // channel may also have refs to itself held internally for various
  // things that need to be cleaned up at channel destruction (e.g.,
  // LB policies, subchannels, etc), and because these refs are not
  // visible to the wrapped language, it cannot be responsible for
  // deferring grpc_shutdown() until after they are released.  To
  // accommodate that, we call grpc_init() here and then call
  // grpc_shutdown() when the channel is actually destroyed, thus
  // ensuring that shutdown is deferred until that point.
  InitInternally();
  RefCountedPtr<channelz::ChannelNode> node;
  if (channelz_node() != nullptr) {
    node = channelz_node()->RefAsSubclass<channelz::ChannelNode>();
  }
  *channel_stack_->on_destroy = [node = std::move(node)]() {
    if (node != nullptr) {
      node->AddTraceEvent(channelz::ChannelTrace::Severity::Info,
                          grpc_slice_from_static_string("Channel destroyed"));
    }
    ShutdownInternally();
  };
}

void LegacyChannel::Orphan() {
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  op->disconnect_with_error = GRPC_ERROR_CREATE("Channel Destroyed");
  grpc_channel_element* elem =
      grpc_channel_stack_element(channel_stack_.get(), 0);
  elem->filter->start_transport_op(elem, op);
  Unref();
}

bool LegacyChannel::IsLame() const {
  grpc_channel_element* elem =
      grpc_channel_stack_last_element(channel_stack_.get());
  return elem->filter == &LameClientFilter::kFilter;
}

grpc_call* LegacyChannel::CreateCall(
    grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* cq, grpc_pollset_set* pollset_set_alternative,
    Slice path, absl::optional<Slice> authority, Timestamp deadline,
    bool registered_method) {
  GPR_ASSERT(is_client_);
  GPR_ASSERT(!(cq != nullptr && pollset_set_alternative != nullptr));
  grpc_call_create_args args;
  args.channel = Ref();
  args.server = nullptr;
  args.parent = parent_call;
  args.propagation_mask = propagation_mask;
  args.cq = cq;
  args.pollset_set_alternative = pollset_set_alternative;
  args.server_transport_data = nullptr;
  args.path = std::move(path);
  args.authority = std::move(authority);
  args.send_deadline = deadline;
  args.registered_method = registered_method;
  grpc_call* call;
  GRPC_LOG_IF_ERROR("call_create", grpc_call_create(&args, &call));
  return call;
}

grpc_connectivity_state LegacyChannel::CheckConnectivityState(
    bool try_to_connect) {
  // Forward through to the underlying client channel.
  ClientChannelFilter* client_channel = GetClientChannelFilter();
  if (GPR_UNLIKELY(client_channel == nullptr)) {
    if (IsLame()) return GRPC_CHANNEL_TRANSIENT_FAILURE;
    gpr_log(GPR_ERROR,
            "grpc_channel_check_connectivity_state called on something that is "
            "not a client channel");
    return GRPC_CHANNEL_SHUTDOWN;
  }
  return client_channel->CheckConnectivityState(try_to_connect);
}

bool LegacyChannel::SupportsConnectivityWatcher() const {
  return GetClientChannelFilter() != nullptr;
}

// A fire-and-forget object to handle external connectivity state watches.
class LegacyChannel::StateWatcher : public DualRefCounted<StateWatcher> {
 public:
  StateWatcher(RefCountedPtr<LegacyChannel> channel, grpc_completion_queue* cq,
               void* tag, grpc_connectivity_state last_observed_state,
               Timestamp deadline)
      : channel_(std::move(channel)),
        cq_(cq),
        tag_(tag),
        state_(last_observed_state) {
    GPR_ASSERT(grpc_cq_begin_op(cq, tag));
    GRPC_CLOSURE_INIT(&on_complete_, WatchComplete, this, nullptr);
    ClientChannelFilter* client_channel = channel_->GetClientChannelFilter();
    if (client_channel == nullptr) {
      // If the target URI used to create the channel was invalid, channel
      // stack initialization failed, and that caused us to create a lame
      // channel.  In that case, connectivity state will never change (it
      // will always be TRANSIENT_FAILURE), so we don't actually start a
      // watch, but we are hiding that fact from the application.
      if (channel_->IsLame()) {
        // A ref is held by the timer callback.
        StartTimer(deadline);
        // Ref from object creation needs to be freed here since lame channel
        // does not have a watcher.
        Unref();
        return;
      }
      Crash(
          "grpc_channel_watch_connectivity_state called on something that is "
          "not a client channel");
    }
    // Ref from object creation is held by the watcher callback.
    auto* watcher_timer_init_state = new WatcherTimerInitState(this, deadline);
    client_channel->AddExternalConnectivityWatcher(
        grpc_polling_entity_create_from_pollset(grpc_cq_pollset(cq)), &state_,
        &on_complete_, watcher_timer_init_state->closure());
  }

 private:
  // A fire-and-forget object used to delay starting the timer until the
  // ClientChannelFilter actually starts the watch.
  class WatcherTimerInitState {
   public:
    WatcherTimerInitState(StateWatcher* state_watcher, Timestamp deadline)
        : state_watcher_(state_watcher), deadline_(deadline) {
      GRPC_CLOSURE_INIT(&closure_, WatcherTimerInit, this, nullptr);
    }

    grpc_closure* closure() { return &closure_; }

   private:
    static void WatcherTimerInit(void* arg, grpc_error_handle /*error*/) {
      auto* self = static_cast<WatcherTimerInitState*>(arg);
      self->state_watcher_->StartTimer(self->deadline_);
      delete self;
    }

    StateWatcher* state_watcher_;
    Timestamp deadline_;
    grpc_closure closure_;
  };

  void StartTimer(Timestamp deadline) {
    const Duration timeout = deadline - Timestamp::Now();
    MutexLock lock(&mu_);
    timer_handle_ =
        channel_->event_engine()->RunAfter(timeout, [self = Ref()]() mutable {
          ApplicationCallbackExecCtx callback_exec_ctx;
          ExecCtx exec_ctx;
          self->TimeoutComplete();
          // StateWatcher deletion might require an active ExecCtx.
          self.reset();
        });
  }

  void TimeoutComplete() {
    timer_fired_ = true;
    // If this is a client channel (not a lame channel), cancel the watch.
    ClientChannelFilter* client_channel = channel_->GetClientChannelFilter();
    if (client_channel != nullptr) {
      client_channel->CancelExternalConnectivityWatcher(&on_complete_);
    }
  }

  static void WatchComplete(void* arg, grpc_error_handle error) {
    RefCountedPtr<StateWatcher> self(static_cast<StateWatcher*>(arg));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_operation_failures)) {
      GRPC_LOG_IF_ERROR("watch_completion_error", error);
    }
    MutexLock lock(&self->mu_);
    if (self->timer_handle_.has_value()) {
      self->channel_->event_engine()->Cancel(*self->timer_handle_);
    }
  }

  // Invoked when both strong refs are released.
  void Orphan() override {
    WeakRef().release();  // Take a weak ref until completion is finished.
    grpc_error_handle error =
        timer_fired_
            ? GRPC_ERROR_CREATE("Timed out waiting for connection state change")
            : absl::OkStatus();
    grpc_cq_end_op(cq_, tag_, error, FinishedCompletion, this,
                   &completion_storage_);
  }

  // Called when the completion is returned to the CQ.
  static void FinishedCompletion(void* arg, grpc_cq_completion* /*ignored*/) {
    auto* self = static_cast<StateWatcher*>(arg);
    self->WeakUnref();
  }

  RefCountedPtr<LegacyChannel> channel_;
  grpc_completion_queue* cq_;
  void* tag_;

  grpc_connectivity_state state_;
  grpc_cq_completion completion_storage_;
  grpc_closure on_complete_;

  // timer_handle_ might be accessed in parallel from multiple threads, e.g.
  // timer callback fired immediately on an EventEngine thread before
  // RunAfter() returns.
  Mutex mu_;
  absl::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      timer_handle_ ABSL_GUARDED_BY(mu_);
  bool timer_fired_ = false;
};

void LegacyChannel::WatchConnectivityState(
    grpc_connectivity_state last_observed_state, Timestamp deadline,
    grpc_completion_queue* cq, void* tag) {
  new StateWatcher(RefAsSubclass<LegacyChannel>(), cq, tag, last_observed_state,
                   deadline);
}

void LegacyChannel::AddConnectivityWatcher(
    grpc_connectivity_state initial_state,
    OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher) {
  auto* client_channel = GetClientChannelFilter();
  GPR_ASSERT(client_channel != nullptr);
  client_channel->AddConnectivityWatcher(initial_state, std::move(watcher));
}

void LegacyChannel::RemoveConnectivityWatcher(
    AsyncConnectivityStateWatcherInterface* watcher) {
  auto* client_channel = GetClientChannelFilter();
  GPR_ASSERT(client_channel != nullptr);
  client_channel->RemoveConnectivityWatcher(watcher);
}

void LegacyChannel::GetInfo(const grpc_channel_info* channel_info) {
  grpc_channel_element* elem =
      grpc_channel_stack_element(channel_stack_.get(), 0);
  elem->filter->get_channel_info(elem, channel_info);
}

void LegacyChannel::ResetConnectionBackoff() {
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  op->reset_connect_backoff = true;
  grpc_channel_element* elem =
      grpc_channel_stack_element(channel_stack_.get(), 0);
  elem->filter->start_transport_op(elem, op);
}

namespace {

struct ping_result {
  grpc_closure closure;
  void* tag;
  grpc_completion_queue* cq;
  grpc_cq_completion completion_storage;
};
void ping_destroy(void* arg, grpc_cq_completion* /*storage*/) { gpr_free(arg); }

void ping_done(void* arg, grpc_error_handle error) {
  ping_result* pr = static_cast<ping_result*>(arg);
  grpc_cq_end_op(pr->cq, pr->tag, error, ping_destroy, pr,
                 &pr->completion_storage);
}

}  // namespace

void LegacyChannel::Ping(grpc_completion_queue* cq, void* tag) {
  ping_result* pr = static_cast<ping_result*>(gpr_malloc(sizeof(*pr)));
  pr->tag = tag;
  pr->cq = cq;
  GRPC_CLOSURE_INIT(&pr->closure, ping_done, pr, grpc_schedule_on_exec_ctx);
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  op->send_ping.on_ack = &pr->closure;
  op->bind_pollset = grpc_cq_pollset(cq);
  GPR_ASSERT(grpc_cq_begin_op(cq, tag));
  grpc_channel_element* top_elem =
      grpc_channel_stack_element(channel_stack_.get(), 0);
  top_elem->filter->start_transport_op(top_elem, op);
}

ClientChannelFilter* LegacyChannel::GetClientChannelFilter() const {
  grpc_channel_element* elem =
      grpc_channel_stack_last_element(channel_stack_.get());
  if (elem->filter != &ClientChannelFilter::kFilterVtableWithPromises &&
      elem->filter != &ClientChannelFilter::kFilterVtableWithoutPromises) {
    return nullptr;
  }
  return static_cast<ClientChannelFilter*>(elem->channel_data);
}

}  // namespace grpc_core
