//
// Copyright 2016 gRPC authors.
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

/// Implementation of the gRPC LB policy.
///
/// This policy takes as input a list of resolved addresses, which must
/// include at least one balancer address.
///
/// An internal channel (\a lb_channel_) is created for the addresses
/// from that are balancers.  This channel behaves just like a regular
/// channel that uses pick_first to select from the list of balancer
/// addresses.
///
/// When we get our initial update, we instantiate the internal *streaming*
/// call to the LB server (whichever address pick_first chose).  The call
/// will be complete when either the balancer sends status or when we cancel
/// the call (e.g., because we are shutting down).  In needed, we retry the
/// call.  If we received at least one valid message from the server, a new
/// call attempt will be made immediately; otherwise, we apply back-off
/// delays between attempts.
///
/// We maintain an internal round_robin policy instance for distributing
/// requests across backends.  Whenever we receive a new serverlist from
/// the balancer, we update the round_robin policy with the new list of
/// addresses.  If we cannot communicate with the balancer on startup,
/// however, we may enter fallback mode, in which case we will populate
/// the child policy's addresses from the backend addresses returned by the
/// resolver.
///
/// Once a child policy instance is in place (and getting updated as described),
/// calls for a pick, a ping, or a cancellation will be serviced right
/// away by forwarding them to the child policy instance.  Any time there's no
/// child policy available (i.e., right after the creation of the gRPCLB
/// policy), pick requests are queued.
///
/// \see https://github.com/grpc/grpc/blob/master/doc/load-balancing.md for the
/// high level design and details.

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/json.h>

// IWYU pragma: no_include <sys/socket.h>

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "upb/upb.hpp"

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/load_balancing/delegating_helper.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/uri/uri_parser.h"

#define GRPC_GRPCLB_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_GRPCLB_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_GRPCLB_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_GRPCLB_RECONNECT_JITTER 0.2
#define GRPC_GRPCLB_DEFAULT_FALLBACK_TIMEOUT_MS 10000
#define GRPC_GRPCLB_DEFAULT_SUBCHANNEL_DELETION_DELAY_MS 10000

// Channel arg used to enable load reporting filter.
#define GRPC_ARG_GRPCLB_ENABLE_LOAD_REPORTING_FILTER \
  "grpc.internal.grpclb_enable_load_reporting_filter"

namespace grpc_core {

TraceFlag grpc_lb_glb_trace(false, "glb");

namespace {

using ::grpc_event_engine::experimental::EventEngine;

constexpr absl::string_view kGrpclb = "grpclb";

class GrpcLbConfig : public LoadBalancingPolicy::Config {
 public:
  GrpcLbConfig() = default;

  GrpcLbConfig(const GrpcLbConfig&) = delete;
  GrpcLbConfig& operator=(const GrpcLbConfig&) = delete;

  GrpcLbConfig(GrpcLbConfig&& other) = delete;
  GrpcLbConfig& operator=(GrpcLbConfig&& other) = delete;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader =
        JsonObjectLoader<GrpcLbConfig>()
            // Note: "childPolicy" field requires custom parsing, so
            // it's handled in JsonPostLoad() instead.
            .OptionalField("serviceName", &GrpcLbConfig::service_name_)
            .Finish();
    return loader;
  }

  void JsonPostLoad(const Json& json, const JsonArgs&,
                    ValidationErrors* errors) {
    ValidationErrors::ScopedField field(errors, ".childPolicy");
    Json child_policy_config_json_tmp;
    const Json* child_policy_config_json;
    auto it = json.object().find("childPolicy");
    if (it == json.object().end()) {
      child_policy_config_json_tmp = Json::FromArray({Json::FromObject({
          {"round_robin", Json::FromObject({})},
      })});
      child_policy_config_json = &child_policy_config_json_tmp;
    } else {
      child_policy_config_json = &it->second;
    }
    auto child_policy_config =
        CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
            *child_policy_config_json);
    if (!child_policy_config.ok()) {
      errors->AddError(child_policy_config.status().message());
      return;
    }
    child_policy_ = std::move(*child_policy_config);
  }

  absl::string_view name() const override { return kGrpclb; }

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }

  const std::string& service_name() const { return service_name_; }

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
  std::string service_name_;
};

class GrpcLb : public LoadBalancingPolicy {
 public:
  explicit GrpcLb(Args args);

  absl::string_view name() const override { return kGrpclb; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  /// Contains a call to the LB server and all the data related to the call.
  class BalancerCallState : public InternallyRefCounted<BalancerCallState> {
   public:
    explicit BalancerCallState(
        RefCountedPtr<LoadBalancingPolicy> parent_grpclb_policy);
    ~BalancerCallState() override;

    // It's the caller's responsibility to ensure that Orphan() is called from
    // inside the combiner.
    void Orphan() override;

    void StartQuery();

    GrpcLbClientStats* client_stats() const { return client_stats_.get(); }

    bool seen_initial_response() const { return seen_initial_response_; }
    bool seen_serverlist() const { return seen_serverlist_; }

   private:
    GrpcLb* grpclb_policy() const {
      return static_cast<GrpcLb*>(grpclb_policy_.get());
    }

    void ScheduleNextClientLoadReportLocked();
    void SendClientLoadReportLocked();

    // EventEngine callbacks
    void MaybeSendClientLoadReportLocked();

    static void ClientLoadReportDone(void* arg, grpc_error_handle error);
    static void OnInitialRequestSent(void* arg, grpc_error_handle error);
    static void OnBalancerMessageReceived(void* arg, grpc_error_handle error);
    static void OnBalancerStatusReceived(void* arg, grpc_error_handle error);

    void ClientLoadReportDoneLocked(grpc_error_handle error);
    void OnInitialRequestSentLocked();
    void OnBalancerMessageReceivedLocked();
    void OnBalancerStatusReceivedLocked(grpc_error_handle error);

    // The owning LB policy.
    RefCountedPtr<LoadBalancingPolicy> grpclb_policy_;

    // The streaming call to the LB server. Always non-NULL.
    grpc_call* lb_call_ = nullptr;

    // recv_initial_metadata
    grpc_metadata_array lb_initial_metadata_recv_;

    // send_message
    grpc_byte_buffer* send_message_payload_ = nullptr;
    grpc_closure lb_on_initial_request_sent_;

    // recv_message
    grpc_byte_buffer* recv_message_payload_ = nullptr;
    grpc_closure lb_on_balancer_message_received_;
    bool seen_initial_response_ = false;
    bool seen_serverlist_ = false;

    // recv_trailing_metadata
    grpc_closure lb_on_balancer_status_received_;
    grpc_metadata_array lb_trailing_metadata_recv_;
    grpc_status_code lb_call_status_;
    grpc_slice lb_call_status_details_;

    // The stats for client-side load reporting associated with this LB call.
    // Created after the first serverlist is received.
    RefCountedPtr<GrpcLbClientStats> client_stats_;
    Duration client_stats_report_interval_;
    absl::optional<EventEngine::TaskHandle> client_load_report_handle_;
    bool last_client_load_report_counters_were_zero_ = false;
    bool client_load_report_is_due_ = false;
    // The closure used for the completion of sending the load report.
    grpc_closure client_load_report_done_closure_;
  };

  class SubchannelWrapper : public DelegatingSubchannel {
   public:
    SubchannelWrapper(RefCountedPtr<SubchannelInterface> subchannel,
                      RefCountedPtr<GrpcLb> lb_policy, std::string lb_token,
                      RefCountedPtr<GrpcLbClientStats> client_stats)
        : DelegatingSubchannel(std::move(subchannel)),
          lb_policy_(std::move(lb_policy)),
          lb_token_(std::move(lb_token)),
          client_stats_(std::move(client_stats)) {}

    ~SubchannelWrapper() override {
      if (!lb_policy_->shutting_down_) {
        lb_policy_->CacheDeletedSubchannelLocked(wrapped_subchannel());
      }
    }

    const std::string& lb_token() const { return lb_token_; }
    GrpcLbClientStats* client_stats() const { return client_stats_.get(); }

   private:
    RefCountedPtr<GrpcLb> lb_policy_;
    std::string lb_token_;
    RefCountedPtr<GrpcLbClientStats> client_stats_;
  };

  class TokenAndClientStatsArg : public RefCounted<TokenAndClientStatsArg> {
   public:
    TokenAndClientStatsArg(std::string lb_token,
                           RefCountedPtr<GrpcLbClientStats> client_stats)
        : lb_token_(std::move(lb_token)),
          client_stats_(std::move(client_stats)) {}

    static absl::string_view ChannelArgName() {
      return GRPC_ARG_NO_SUBCHANNEL_PREFIX "grpclb_token_and_client_stats";
    }

    static int ChannelArgsCompare(const TokenAndClientStatsArg* a,
                                  const TokenAndClientStatsArg* b) {
      int r = a->lb_token_.compare(b->lb_token_);
      if (r != 0) return r;
      return QsortCompare(a->client_stats_.get(), b->client_stats_.get());
    }

    const std::string& lb_token() const { return lb_token_; }
    RefCountedPtr<GrpcLbClientStats> client_stats() const {
      return client_stats_;
    }

   private:
    std::string lb_token_;
    RefCountedPtr<GrpcLbClientStats> client_stats_;
  };

  class Serverlist : public RefCounted<Serverlist> {
   public:
    // Takes ownership of serverlist.
    explicit Serverlist(std::vector<GrpcLbServer> serverlist)
        : serverlist_(std::move(serverlist)) {}

    bool operator==(const Serverlist& other) const;

    const std::vector<GrpcLbServer>& serverlist() const { return serverlist_; }

    // Returns a text representation suitable for logging.
    std::string AsText() const;

    // Extracts all non-drop entries into a ServerAddressList.
    ServerAddressList GetServerAddressList(
        GrpcLbClientStats* client_stats) const;

    // Returns true if the serverlist contains at least one drop entry and
    // no backend address entries.
    bool ContainsAllDropEntries() const;

    // Returns the LB token to use for a drop, or null if the call
    // should not be dropped.
    //
    // Note: This is called from the picker, NOT from inside the control
    // plane work_serializer.
    const char* ShouldDrop();

   private:
    std::vector<GrpcLbServer> serverlist_;

    // Accessed from the picker, so needs synchronization.
    std::atomic<size_t> drop_index_{0};
  };

  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<Serverlist> serverlist,
           RefCountedPtr<SubchannelPicker> child_picker,
           RefCountedPtr<GrpcLbClientStats> client_stats)
        : serverlist_(std::move(serverlist)),
          child_picker_(std::move(child_picker)),
          client_stats_(std::move(client_stats)) {}

    PickResult Pick(PickArgs args) override;

   private:
    // A subchannel call tracker that unrefs the GrpcLbClientStats object
    // in the case where the subchannel call is never actually started,
    // since the client load reporting filter will not be able to do it
    // in that case.
    class SubchannelCallTracker : public SubchannelCallTrackerInterface {
     public:
      SubchannelCallTracker(
          RefCountedPtr<GrpcLbClientStats> client_stats,
          std::unique_ptr<SubchannelCallTrackerInterface> original_call_tracker)
          : client_stats_(std::move(client_stats)),
            original_call_tracker_(std::move(original_call_tracker)) {}

      void Start() override {
        if (original_call_tracker_ != nullptr) {
          original_call_tracker_->Start();
        }
        // If we're actually starting the subchannel call, then the
        // client load reporting filter will take ownership of the ref
        // passed down to it via metadata.
        client_stats_.release();
      }

      void Finish(FinishArgs args) override {
        if (original_call_tracker_ != nullptr) {
          original_call_tracker_->Finish(args);
        }
      }

     private:
      RefCountedPtr<GrpcLbClientStats> client_stats_;
      std::unique_ptr<SubchannelCallTrackerInterface> original_call_tracker_;
    };

    // Serverlist to be used for determining drops.
    RefCountedPtr<Serverlist> serverlist_;

    RefCountedPtr<SubchannelPicker> child_picker_;
    RefCountedPtr<GrpcLbClientStats> client_stats_;
  };

  class Helper : public ParentOwningDelegatingChannelControlHelper<GrpcLb> {
   public:
    explicit Helper(RefCountedPtr<GrpcLb> parent)
        : ParentOwningDelegatingChannelControlHelper(std::move(parent)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const ChannelArgs& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
  };

  class StateWatcher : public AsyncConnectivityStateWatcherInterface {
   public:
    explicit StateWatcher(RefCountedPtr<GrpcLb> parent)
        : AsyncConnectivityStateWatcherInterface(parent->work_serializer()),
          parent_(std::move(parent)) {}

    ~StateWatcher() override { parent_.reset(DEBUG_LOCATION, "StateWatcher"); }

   private:
    void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                   const absl::Status& status) override {
      if (parent_->fallback_at_startup_checks_pending_ &&
          new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        // In TRANSIENT_FAILURE.  Cancel the fallback timer and go into
        // fallback mode immediately.
        gpr_log(GPR_INFO,
                "[grpclb %p] balancer channel in state:TRANSIENT_FAILURE (%s); "
                "entering fallback mode",
                parent_.get(), status.ToString().c_str());
        parent_->fallback_at_startup_checks_pending_ = false;
        parent_->channel_control_helper()->GetEventEngine()->Cancel(
            *parent_->lb_fallback_timer_handle_);
        parent_->fallback_mode_ = true;
        parent_->CreateOrUpdateChildPolicyLocked();
        // Cancel the watch, since we don't care about the channel state once we
        // go into fallback mode.
        parent_->CancelBalancerChannelConnectivityWatchLocked();
      }
    }

    RefCountedPtr<GrpcLb> parent_;
  };

  void ShutdownLocked() override;

  // Helper functions used in UpdateLocked().
  absl::Status UpdateBalancerChannelLocked();

  void CancelBalancerChannelConnectivityWatchLocked();

  // Methods for dealing with fallback state.
  void MaybeEnterFallbackModeAfterStartup();
  void OnFallbackTimerLocked();

  // Methods for dealing with the balancer call.
  void StartBalancerCallLocked();
  void StartBalancerCallRetryTimerLocked();
  void OnBalancerCallRetryTimerLocked();

  // Methods for dealing with the child policy.
  ChannelArgs CreateChildPolicyArgsLocked(
      bool is_backend_from_grpclb_load_balancer);
  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const ChannelArgs& args);
  void CreateOrUpdateChildPolicyLocked();

  // Subchannel caching.
  void CacheDeletedSubchannelLocked(
      RefCountedPtr<SubchannelInterface> subchannel);
  void StartSubchannelCacheTimerLocked();
  void OnSubchannelCacheTimerLocked();

  // Who the client is trying to communicate with.
  std::string server_name_;
  // Configurations for the policy.
  RefCountedPtr<GrpcLbConfig> config_;

  // Current channel args from the resolver.
  ChannelArgs args_;

  // Internal state.
  bool shutting_down_ = false;

  // The channel for communicating with the LB server.
  grpc_channel* lb_channel_ = nullptr;
  StateWatcher* watcher_ = nullptr;
  // Response generator to inject address updates into lb_channel_.
  RefCountedPtr<FakeResolverResponseGenerator> response_generator_;
  // Parent channelz node.
  RefCountedPtr<channelz::ChannelNode> parent_channelz_node_;

  // The data associated with the current LB call. It holds a ref to this LB
  // policy. It's initialized every time we query for backends. It's reset to
  // NULL whenever the current LB call is no longer needed (e.g., the LB policy
  // is shutting down, or the LB call has ended). A non-NULL lb_calld_ always
  // contains a non-NULL lb_call_.
  OrphanablePtr<BalancerCallState> lb_calld_;
  // Timeout for the LB call. 0 means no deadline.
  const Duration lb_call_timeout_;
  // Balancer call retry state.
  BackOff lb_call_backoff_;
  absl::optional<EventEngine::TaskHandle> lb_call_retry_timer_handle_;

  // The deserialized response from the balancer. May be nullptr until one
  // such response has arrived.
  RefCountedPtr<Serverlist> serverlist_;

  // Whether we're in fallback mode.
  bool fallback_mode_ = false;
  // The backend addresses from the resolver.
  absl::StatusOr<ServerAddressList> fallback_backend_addresses_;
  // The last resolution note from our parent.
  // To be passed to child policy when fallback_backend_addresses_ is empty.
  std::string resolution_note_;
  // State for fallback-at-startup checks.
  // Timeout after startup after which we will go into fallback mode if
  // we have not received a serverlist from the balancer.
  const Duration fallback_at_startup_timeout_;
  bool fallback_at_startup_checks_pending_ = false;
  absl::optional<EventEngine::TaskHandle> lb_fallback_timer_handle_;

  // The child policy to use for the backends.
  OrphanablePtr<LoadBalancingPolicy> child_policy_;
  // Child policy in state READY.
  bool child_policy_ready_ = false;

  // Deleted subchannel caching.
  const Duration subchannel_cache_interval_;
  std::map<Timestamp /*deletion time*/,
           std::vector<RefCountedPtr<SubchannelInterface>>>
      cached_subchannels_;
  absl::optional<EventEngine::TaskHandle> subchannel_cache_timer_handle_;
};

//
// GrpcLb::Serverlist
//

bool GrpcLb::Serverlist::operator==(const Serverlist& other) const {
  return serverlist_ == other.serverlist_;
}

void ParseServer(const GrpcLbServer& server, grpc_resolved_address* addr) {
  memset(addr, 0, sizeof(*addr));
  if (server.drop) return;
  const uint16_t netorder_port = grpc_htons(static_cast<uint16_t>(server.port));
  // the addresses are given in binary format (a in(6)_addr struct) in
  // server->ip_address.bytes.
  if (server.ip_size == 4) {
    addr->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in));
    grpc_sockaddr_in* addr4 = reinterpret_cast<grpc_sockaddr_in*>(&addr->addr);
    addr4->sin_family = GRPC_AF_INET;
    memcpy(&addr4->sin_addr, server.ip_addr, server.ip_size);
    addr4->sin_port = netorder_port;
  } else if (server.ip_size == 16) {
    addr->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in6));
    grpc_sockaddr_in6* addr6 =
        reinterpret_cast<grpc_sockaddr_in6*>(&addr->addr);
    addr6->sin6_family = GRPC_AF_INET6;
    memcpy(&addr6->sin6_addr, server.ip_addr, server.ip_size);
    addr6->sin6_port = netorder_port;
  }
}

std::string GrpcLb::Serverlist::AsText() const {
  std::vector<std::string> entries;
  for (size_t i = 0; i < serverlist_.size(); ++i) {
    const GrpcLbServer& server = serverlist_[i];
    std::string ipport;
    if (server.drop) {
      ipport = "(drop)";
    } else {
      grpc_resolved_address addr;
      ParseServer(server, &addr);
      auto addr_str = grpc_sockaddr_to_string(&addr, false);
      ipport = addr_str.ok() ? *addr_str : addr_str.status().ToString();
    }
    entries.push_back(absl::StrFormat("  %" PRIuPTR ": %s token=%s\n", i,
                                      ipport, server.load_balance_token));
  }
  return absl::StrJoin(entries, "");
}

bool IsServerValid(const GrpcLbServer& server, size_t idx, bool log) {
  if (server.drop) return false;
  if (GPR_UNLIKELY(server.port >> 16 != 0)) {
    if (log) {
      gpr_log(GPR_ERROR,
              "Invalid port '%d' at index %" PRIuPTR
              " of serverlist. Ignoring.",
              server.port, idx);
    }
    return false;
  }
  if (GPR_UNLIKELY(server.ip_size != 4 && server.ip_size != 16)) {
    if (log) {
      gpr_log(GPR_ERROR,
              "Expected IP to be 4 or 16 bytes, got %d at index %" PRIuPTR
              " of serverlist. Ignoring",
              server.ip_size, idx);
    }
    return false;
  }
  return true;
}

// Returns addresses extracted from the serverlist.
ServerAddressList GrpcLb::Serverlist::GetServerAddressList(
    GrpcLbClientStats* client_stats) const {
  RefCountedPtr<GrpcLbClientStats> stats;
  if (client_stats != nullptr) stats = client_stats->Ref();
  ServerAddressList addresses;
  for (size_t i = 0; i < serverlist_.size(); ++i) {
    const GrpcLbServer& server = serverlist_[i];
    if (!IsServerValid(server, i, false)) continue;
    // Address processing.
    grpc_resolved_address addr;
    ParseServer(server, &addr);
    // LB token processing.
    const size_t lb_token_length = strnlen(
        server.load_balance_token, GPR_ARRAY_SIZE(server.load_balance_token));
    std::string lb_token(server.load_balance_token, lb_token_length);
    if (lb_token.empty()) {
      auto addr_uri = grpc_sockaddr_to_uri(&addr);
      gpr_log(GPR_INFO,
              "Missing LB token for backend address '%s'. The empty token will "
              "be used instead",
              addr_uri.ok() ? addr_uri->c_str()
                            : addr_uri.status().ToString().c_str());
    }
    // Add address with a channel arg containing LB token and stats object.
    addresses.emplace_back(
        addr, ChannelArgs().SetObject(MakeRefCounted<TokenAndClientStatsArg>(
                  std::move(lb_token), stats)));
  }
  return addresses;
}

bool GrpcLb::Serverlist::ContainsAllDropEntries() const {
  if (serverlist_.empty()) return false;
  for (const GrpcLbServer& server : serverlist_) {
    if (!server.drop) return false;
  }
  return true;
}

const char* GrpcLb::Serverlist::ShouldDrop() {
  if (serverlist_.empty()) return nullptr;
  size_t index = drop_index_.fetch_add(1, std::memory_order_relaxed);
  GrpcLbServer& server = serverlist_[index % serverlist_.size()];
  return server.drop ? server.load_balance_token : nullptr;
}

//
// GrpcLb::Picker
//

GrpcLb::PickResult GrpcLb::Picker::Pick(PickArgs args) {
  // Check if we should drop the call.
  const char* drop_token =
      serverlist_ == nullptr ? nullptr : serverlist_->ShouldDrop();
  if (drop_token != nullptr) {
    // Update client load reporting stats to indicate the number of
    // dropped calls.  Note that we have to do this here instead of in
    // the client_load_reporting filter, because we do not create a
    // subchannel call (and therefore no client_load_reporting filter)
    // for dropped calls.
    if (client_stats_ != nullptr) {
      client_stats_->AddCallDropped(drop_token);
    }
    return PickResult::Drop(
        absl::UnavailableError("drop directed by grpclb balancer"));
  }
  // Forward pick to child policy.
  PickResult result = child_picker_->Pick(args);
  // If pick succeeded, add LB token to initial metadata.
  auto* complete_pick = absl::get_if<PickResult::Complete>(&result.result);
  if (complete_pick != nullptr) {
    const SubchannelWrapper* subchannel_wrapper =
        static_cast<SubchannelWrapper*>(complete_pick->subchannel.get());
    // Encode client stats object into metadata for use by
    // client_load_reporting filter.
    GrpcLbClientStats* client_stats = subchannel_wrapper->client_stats();
    if (client_stats != nullptr) {
      complete_pick->subchannel_call_tracker =
          std::make_unique<SubchannelCallTracker>(
              client_stats->Ref(),
              std::move(complete_pick->subchannel_call_tracker));
      // The metadata value is a hack: we pretend the pointer points to
      // a string and rely on the client_load_reporting filter to know
      // how to interpret it.
      args.initial_metadata->Add(
          GrpcLbClientStatsMetadata::key(),
          absl::string_view(reinterpret_cast<const char*>(client_stats), 0));
      // Update calls-started.
      client_stats->AddCallStarted();
    }
    // Encode the LB token in metadata.
    // Create a new copy on the call arena, since the subchannel list
    // may get refreshed between when we return this pick and when the
    // initial metadata goes out on the wire.
    if (!subchannel_wrapper->lb_token().empty()) {
      char* lb_token = static_cast<char*>(
          args.call_state->Alloc(subchannel_wrapper->lb_token().size() + 1));
      strcpy(lb_token, subchannel_wrapper->lb_token().c_str());
      args.initial_metadata->Add(LbTokenMetadata::key(), lb_token);
    }
    // Unwrap subchannel to pass up to the channel.
    complete_pick->subchannel = subchannel_wrapper->wrapped_subchannel();
  }
  return result;
}

//
// GrpcLb::Helper
//

RefCountedPtr<SubchannelInterface> GrpcLb::Helper::CreateSubchannel(
    ServerAddress address, const ChannelArgs& args) {
  if (parent()->shutting_down_) return nullptr;
  const auto* arg = address.args().GetObject<TokenAndClientStatsArg>();
  if (arg == nullptr) {
    Crash(
        absl::StrFormat("[grpclb %p] no TokenAndClientStatsArg for address %p",
                        parent(), address.ToString().c_str()));
  }
  std::string lb_token = arg->lb_token();
  RefCountedPtr<GrpcLbClientStats> client_stats = arg->client_stats();
  return MakeRefCounted<SubchannelWrapper>(
      parent()->channel_control_helper()->CreateSubchannel(std::move(address),
                                                           args),
      parent()->Ref(DEBUG_LOCATION, "SubchannelWrapper"), std::move(lb_token),
      std::move(client_stats));
}

void GrpcLb::Helper::UpdateState(grpc_connectivity_state state,
                                 const absl::Status& status,
                                 RefCountedPtr<SubchannelPicker> picker) {
  if (parent()->shutting_down_) return;
  // Record whether child policy reports READY.
  parent()->child_policy_ready_ = state == GRPC_CHANNEL_READY;
  // Enter fallback mode if needed.
  parent()->MaybeEnterFallbackModeAfterStartup();
  // We pass the serverlist to the picker so that it can handle drops.
  // However, we don't want to handle drops in the case where the child
  // policy is reporting a state other than READY (unless we are
  // dropping *all* calls), because we don't want to process drops for picks
  // that yield a QUEUE result; this would result in dropping too many calls,
  // since we will see the queued picks multiple times, and we'd consider each
  // one a separate call for the drop calculation.  So in this case, we pass
  // a null serverlist to the picker, which tells it not to do drops.
  RefCountedPtr<Serverlist> serverlist;
  if (state == GRPC_CHANNEL_READY ||
      (parent()->serverlist_ != nullptr &&
       parent()->serverlist_->ContainsAllDropEntries())) {
    serverlist = parent()->serverlist_;
  }
  RefCountedPtr<GrpcLbClientStats> client_stats;
  if (parent()->lb_calld_ != nullptr &&
      parent()->lb_calld_->client_stats() != nullptr) {
    client_stats = parent()->lb_calld_->client_stats()->Ref();
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO,
            "[grpclb %p helper %p] state=%s (%s) wrapping child "
            "picker %p (serverlist=%p, client_stats=%p)",
            parent(), this, ConnectivityStateName(state),
            status.ToString().c_str(), picker.get(), serverlist.get(),
            client_stats.get());
  }
  parent()->channel_control_helper()->UpdateState(
      state, status,
      MakeRefCounted<Picker>(std::move(serverlist), std::move(picker),
                             std::move(client_stats)));
}

void GrpcLb::Helper::RequestReresolution() {
  if (parent()->shutting_down_) return;
  // If we are talking to a balancer, we expect to get updated addresses
  // from the balancer, so we can ignore the re-resolution request from
  // the child policy. Otherwise, pass the re-resolution request up to the
  // channel.
  if (parent()->lb_calld_ == nullptr ||
      !parent()->lb_calld_->seen_initial_response()) {
    parent()->channel_control_helper()->RequestReresolution();
  }
}

//
// GrpcLb::BalancerCallState
//

GrpcLb::BalancerCallState::BalancerCallState(
    RefCountedPtr<LoadBalancingPolicy> parent_grpclb_policy)
    : InternallyRefCounted<BalancerCallState>(
          GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace) ? "BalancerCallState"
                                                     : nullptr),
      grpclb_policy_(std::move(parent_grpclb_policy)) {
  GPR_ASSERT(grpclb_policy_ != nullptr);
  GPR_ASSERT(!grpclb_policy()->shutting_down_);
  // Init the LB call. Note that the LB call will progress every time there's
  // activity in grpclb_policy_->interested_parties(), which is comprised of
  // the polling entities from client_channel.
  GPR_ASSERT(!grpclb_policy()->server_name_.empty());
  // Closure Initialization
  GRPC_CLOSURE_INIT(&lb_on_initial_request_sent_, OnInitialRequestSent, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&lb_on_balancer_message_received_,
                    OnBalancerMessageReceived, this, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&lb_on_balancer_status_received_, OnBalancerStatusReceived,
                    this, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&client_load_report_done_closure_, ClientLoadReportDone,
                    this, grpc_schedule_on_exec_ctx);
  const Timestamp deadline =
      grpclb_policy()->lb_call_timeout_ == Duration::Zero()
          ? Timestamp::InfFuture()
          : Timestamp::Now() + grpclb_policy()->lb_call_timeout_;
  lb_call_ = grpc_channel_create_pollset_set_call(
      grpclb_policy()->lb_channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      grpclb_policy_->interested_parties(),
      Slice::FromStaticString("/grpc.lb.v1.LoadBalancer/BalanceLoad").c_slice(),
      nullptr, deadline, nullptr);
  // Init the LB call request payload.
  upb::Arena arena;
  grpc_slice request_payload_slice = GrpcLbRequestCreate(
      grpclb_policy()->config_->service_name().empty()
          ? grpclb_policy()->server_name_.c_str()
          : grpclb_policy()->config_->service_name().c_str(),
      arena.ptr());
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  CSliceUnref(request_payload_slice);
  // Init other data associated with the LB call.
  grpc_metadata_array_init(&lb_initial_metadata_recv_);
  grpc_metadata_array_init(&lb_trailing_metadata_recv_);
}

GrpcLb::BalancerCallState::~BalancerCallState() {
  GPR_ASSERT(lb_call_ != nullptr);
  grpc_call_unref(lb_call_);
  grpc_metadata_array_destroy(&lb_initial_metadata_recv_);
  grpc_metadata_array_destroy(&lb_trailing_metadata_recv_);
  grpc_byte_buffer_destroy(send_message_payload_);
  grpc_byte_buffer_destroy(recv_message_payload_);
  CSliceUnref(lb_call_status_details_);
}

void GrpcLb::BalancerCallState::Orphan() {
  GPR_ASSERT(lb_call_ != nullptr);
  // If we are here because grpclb_policy wants to cancel the call,
  // lb_on_balancer_status_received_ will complete the cancellation and clean
  // up. Otherwise, we are here because grpclb_policy has to orphan a failed
  // call, then the following cancellation will be a no-op.
  grpc_call_cancel_internal(lb_call_);
  if (client_load_report_handle_.has_value() &&
      grpclb_policy()->channel_control_helper()->GetEventEngine()->Cancel(
          client_load_report_handle_.value())) {
    Unref(DEBUG_LOCATION, "client_load_report cancelled");
  }
  // Note that the initial ref is hold by lb_on_balancer_status_received_
  // instead of the caller of this function. So the corresponding unref happens
  // in lb_on_balancer_status_received_ instead of here.
}

void GrpcLb::BalancerCallState::StartQuery() {
  GPR_ASSERT(lb_call_ != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO, "[grpclb %p] lb_calld=%p: Starting LB call %p",
            grpclb_policy_.get(), this, lb_call_);
  }
  // Create the ops.
  grpc_call_error call_error;
  grpc_op ops[3];
  memset(ops, 0, sizeof(ops));
  // Op: send initial metadata.
  grpc_op* op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY |
              GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
  op->reserved = nullptr;
  op++;
  // Op: send request message.
  GPR_ASSERT(send_message_payload_ != nullptr);
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = send_message_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // TODO(roth): We currently track this ref manually.  Once the
  // ClosureRef API is ready, we should pass the RefCountedPtr<> along
  // with the callback.
  auto self = Ref(DEBUG_LOCATION, "on_initial_request_sent");
  self.release();
  call_error = grpc_call_start_batch_and_execute(lb_call_, ops,
                                                 static_cast<size_t>(op - ops),
                                                 &lb_on_initial_request_sent_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: recv initial metadata.
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &lb_initial_metadata_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // Op: recv response.
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_message_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // TODO(roth): We currently track this ref manually.  Once the
  // ClosureRef API is ready, we should pass the RefCountedPtr<> along
  // with the callback.
  self = Ref(DEBUG_LOCATION, "on_message_received");
  self.release();
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, static_cast<size_t>(op - ops),
      &lb_on_balancer_message_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: recv server status.
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &lb_trailing_metadata_recv_;
  op->data.recv_status_on_client.status = &lb_call_status_;
  op->data.recv_status_on_client.status_details = &lb_call_status_details_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // This callback signals the end of the LB call, so it relies on the initial
  // ref instead of a new ref. When it's invoked, it's the initial ref that is
  // unreffed.
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, static_cast<size_t>(op - ops),
      &lb_on_balancer_status_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void GrpcLb::BalancerCallState::ScheduleNextClientLoadReportLocked() {
  client_load_report_handle_ =
      grpclb_policy()->channel_control_helper()->GetEventEngine()->RunAfter(
          client_stats_report_interval_, [this] {
            ApplicationCallbackExecCtx callback_exec_ctx;
            ExecCtx exec_ctx;
            grpclb_policy()->work_serializer()->Run(
                [this] { MaybeSendClientLoadReportLocked(); }, DEBUG_LOCATION);
          });
}

void GrpcLb::BalancerCallState::MaybeSendClientLoadReportLocked() {
  client_load_report_handle_.reset();
  if (this != grpclb_policy()->lb_calld_.get()) {
    Unref(DEBUG_LOCATION, "client_load_report");
    return;
  }
  // If we've already sent the initial request, then we can go ahead and send
  // the load report. Otherwise, we need to wait until the initial request has
  // been sent to send this (see OnInitialRequestSentLocked()).
  if (send_message_payload_ == nullptr) {
    SendClientLoadReportLocked();
  } else {
    client_load_report_is_due_ = true;
  }
}

void GrpcLb::BalancerCallState::SendClientLoadReportLocked() {
  // Construct message payload.
  GPR_ASSERT(send_message_payload_ == nullptr);
  // Get snapshot of stats.
  int64_t num_calls_started;
  int64_t num_calls_finished;
  int64_t num_calls_finished_with_client_failed_to_send;
  int64_t num_calls_finished_known_received;
  std::unique_ptr<GrpcLbClientStats::DroppedCallCounts> drop_token_counts;
  client_stats_->Get(&num_calls_started, &num_calls_finished,
                     &num_calls_finished_with_client_failed_to_send,
                     &num_calls_finished_known_received, &drop_token_counts);
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  if (num_calls_started == 0 && num_calls_finished == 0 &&
      num_calls_finished_with_client_failed_to_send == 0 &&
      num_calls_finished_known_received == 0 &&
      (drop_token_counts == nullptr || drop_token_counts->empty())) {
    if (last_client_load_report_counters_were_zero_) {
      ScheduleNextClientLoadReportLocked();
      return;
    }
    last_client_load_report_counters_were_zero_ = true;
  } else {
    last_client_load_report_counters_were_zero_ = false;
  }
  // Populate load report.
  upb::Arena arena;
  grpc_slice request_payload_slice = GrpcLbLoadReportRequestCreate(
      num_calls_started, num_calls_finished,
      num_calls_finished_with_client_failed_to_send,
      num_calls_finished_known_received, drop_token_counts.get(), arena.ptr());
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  CSliceUnref(request_payload_slice);
  // Send the report.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message = send_message_payload_;
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      lb_call_, &op, 1, &client_load_report_done_closure_);
  if (GPR_UNLIKELY(call_error != GRPC_CALL_OK)) {
    gpr_log(GPR_ERROR,
            "[grpclb %p] lb_calld=%p call_error=%d sending client load report",
            grpclb_policy_.get(), this, call_error);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  }
}

void GrpcLb::BalancerCallState::ClientLoadReportDone(void* arg,
                                                     grpc_error_handle error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  lb_calld->grpclb_policy()->work_serializer()->Run(
      [lb_calld, error]() { lb_calld->ClientLoadReportDoneLocked(error); },
      DEBUG_LOCATION);
}

void GrpcLb::BalancerCallState::ClientLoadReportDoneLocked(
    grpc_error_handle error) {
  grpc_byte_buffer_destroy(send_message_payload_);
  send_message_payload_ = nullptr;
  if (!error.ok() || this != grpclb_policy()->lb_calld_.get()) {
    Unref(DEBUG_LOCATION, "client_load_report");
    return;
  }
  ScheduleNextClientLoadReportLocked();
}

void GrpcLb::BalancerCallState::OnInitialRequestSent(
    void* arg, grpc_error_handle /*error*/) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  lb_calld->grpclb_policy()->work_serializer()->Run(
      [lb_calld]() { lb_calld->OnInitialRequestSentLocked(); }, DEBUG_LOCATION);
}

void GrpcLb::BalancerCallState::OnInitialRequestSentLocked() {
  grpc_byte_buffer_destroy(send_message_payload_);
  send_message_payload_ = nullptr;
  // If we attempted to send a client load report before the initial request was
  // sent (and this lb_calld is still in use), send the load report now.
  if (client_load_report_is_due_ && this == grpclb_policy()->lb_calld_.get()) {
    SendClientLoadReportLocked();
    client_load_report_is_due_ = false;
  }
  Unref(DEBUG_LOCATION, "on_initial_request_sent");
}

void GrpcLb::BalancerCallState::OnBalancerMessageReceived(
    void* arg, grpc_error_handle /*error*/) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  lb_calld->grpclb_policy()->work_serializer()->Run(
      [lb_calld]() { lb_calld->OnBalancerMessageReceivedLocked(); },
      DEBUG_LOCATION);
}

void GrpcLb::BalancerCallState::OnBalancerMessageReceivedLocked() {
  // Null payload means the LB call was cancelled.
  if (this != grpclb_policy()->lb_calld_.get() ||
      recv_message_payload_ == nullptr) {
    Unref(DEBUG_LOCATION, "on_message_received");
    return;
  }
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, recv_message_payload_);
  grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_byte_buffer_reader_destroy(&bbr);
  grpc_byte_buffer_destroy(recv_message_payload_);
  recv_message_payload_ = nullptr;
  GrpcLbResponse response;
  upb::Arena arena;
  if (!GrpcLbResponseParse(response_slice, arena.ptr(), &response) ||
      (response.type == response.INITIAL && seen_initial_response_)) {
    if (gpr_should_log(GPR_LOG_SEVERITY_ERROR)) {
      char* response_slice_str =
          grpc_dump_slice(response_slice, GPR_DUMP_ASCII | GPR_DUMP_HEX);
      gpr_log(GPR_ERROR,
              "[grpclb %p] lb_calld=%p: Invalid LB response received: '%s'. "
              "Ignoring.",
              grpclb_policy(), this, response_slice_str);
      gpr_free(response_slice_str);
    }
  } else {
    switch (response.type) {
      case response.INITIAL: {
        if (response.client_stats_report_interval != Duration::Zero()) {
          client_stats_report_interval_ = std::max(
              Duration::Seconds(1), response.client_stats_report_interval);
          if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
            gpr_log(GPR_INFO,
                    "[grpclb %p] lb_calld=%p: Received initial LB response "
                    "message; client load reporting interval = %" PRId64
                    " milliseconds",
                    grpclb_policy(), this,
                    client_stats_report_interval_.millis());
          }
        } else if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
          gpr_log(GPR_INFO,
                  "[grpclb %p] lb_calld=%p: Received initial LB response "
                  "message; client load reporting NOT enabled",
                  grpclb_policy(), this);
        }
        seen_initial_response_ = true;
        break;
      }
      case response.SERVERLIST: {
        GPR_ASSERT(lb_call_ != nullptr);
        auto serverlist_wrapper =
            MakeRefCounted<Serverlist>(std::move(response.serverlist));
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
          gpr_log(GPR_INFO,
                  "[grpclb %p] lb_calld=%p: Serverlist with %" PRIuPTR
                  " servers received:\n%s",
                  grpclb_policy(), this,
                  serverlist_wrapper->serverlist().size(),
                  serverlist_wrapper->AsText().c_str());
        }
        seen_serverlist_ = true;
        // Start sending client load report only after we start using the
        // serverlist returned from the current LB call.
        if (client_stats_report_interval_ > Duration::Zero() &&
            client_stats_ == nullptr) {
          client_stats_ = MakeRefCounted<GrpcLbClientStats>();
          // Ref held by callback.
          Ref(DEBUG_LOCATION, "client_load_report").release();
          ScheduleNextClientLoadReportLocked();
        }
        // Check if the serverlist differs from the previous one.
        if (grpclb_policy()->serverlist_ != nullptr &&
            *grpclb_policy()->serverlist_ == *serverlist_wrapper) {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
            gpr_log(GPR_INFO,
                    "[grpclb %p] lb_calld=%p: Incoming server list identical "
                    "to current, ignoring.",
                    grpclb_policy(), this);
          }
        } else {  // New serverlist.
          // Dispose of the fallback.
          // TODO(roth): Ideally, we should stay in fallback mode until we
          // know that we can reach at least one of the backends in the new
          // serverlist.  Unfortunately, we can't do that, since we need to
          // send the new addresses to the child policy in order to determine
          // if they are reachable, and if we don't exit fallback mode now,
          // CreateOrUpdateChildPolicyLocked() will use the fallback
          // addresses instead of the addresses from the new serverlist.
          // However, if we can't reach any of the servers in the new
          // serverlist, then the child policy will never switch away from
          // the fallback addresses, but the grpclb policy will still think
          // that we're not in fallback mode, which means that we won't send
          // updates to the child policy when the fallback addresses are
          // updated by the resolver.  This is sub-optimal, but the only way
          // to fix it is to maintain a completely separate child policy for
          // fallback mode, and that's more work than we want to put into
          // the grpclb implementation at this point, since we're deprecating
          // it in favor of the xds policy.  We will implement this the
          // right way in the xds policy instead.
          if (grpclb_policy()->fallback_mode_) {
            gpr_log(GPR_INFO,
                    "[grpclb %p] Received response from balancer; exiting "
                    "fallback mode",
                    grpclb_policy());
            grpclb_policy()->fallback_mode_ = false;
          }
          if (grpclb_policy()->fallback_at_startup_checks_pending_) {
            grpclb_policy()->fallback_at_startup_checks_pending_ = false;
            grpclb_policy()->channel_control_helper()->GetEventEngine()->Cancel(
                *grpclb_policy()->lb_fallback_timer_handle_);
            grpclb_policy()->CancelBalancerChannelConnectivityWatchLocked();
          }
          // Update the serverlist in the GrpcLb instance. This serverlist
          // instance will be destroyed either upon the next update or when the
          // GrpcLb instance is destroyed.
          grpclb_policy()->serverlist_ = std::move(serverlist_wrapper);
          grpclb_policy()->CreateOrUpdateChildPolicyLocked();
        }
        break;
      }
      case response.FALLBACK: {
        if (!grpclb_policy()->fallback_mode_) {
          gpr_log(GPR_INFO,
                  "[grpclb %p] Entering fallback mode as requested by balancer",
                  grpclb_policy());
          if (grpclb_policy()->fallback_at_startup_checks_pending_) {
            grpclb_policy()->fallback_at_startup_checks_pending_ = false;
            grpclb_policy()->channel_control_helper()->GetEventEngine()->Cancel(
                *grpclb_policy()->lb_fallback_timer_handle_);
            grpclb_policy()->CancelBalancerChannelConnectivityWatchLocked();
          }
          grpclb_policy()->fallback_mode_ = true;
          grpclb_policy()->CreateOrUpdateChildPolicyLocked();
          // Reset serverlist, so that if the balancer exits fallback
          // mode by sending the same serverlist we were previously
          // using, we don't incorrectly ignore it as a duplicate.
          grpclb_policy()->serverlist_.reset();
        }
        break;
      }
    }
  }
  CSliceUnref(response_slice);
  if (!grpclb_policy()->shutting_down_) {
    // Keep listening for serverlist updates.
    grpc_op op;
    memset(&op, 0, sizeof(op));
    op.op = GRPC_OP_RECV_MESSAGE;
    op.data.recv_message.recv_message = &recv_message_payload_;
    op.flags = 0;
    op.reserved = nullptr;
    // Reuse the "OnBalancerMessageReceivedLocked" ref taken in StartQuery().
    const grpc_call_error call_error = grpc_call_start_batch_and_execute(
        lb_call_, &op, 1, &lb_on_balancer_message_received_);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  } else {
    Unref(DEBUG_LOCATION, "on_message_received+grpclb_shutdown");
  }
}

void GrpcLb::BalancerCallState::OnBalancerStatusReceived(
    void* arg, grpc_error_handle error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  lb_calld->grpclb_policy()->work_serializer()->Run(
      [lb_calld, error]() { lb_calld->OnBalancerStatusReceivedLocked(error); },
      DEBUG_LOCATION);
}

void GrpcLb::BalancerCallState::OnBalancerStatusReceivedLocked(
    grpc_error_handle error) {
  GPR_ASSERT(lb_call_ != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    char* status_details = grpc_slice_to_c_string(lb_call_status_details_);
    gpr_log(GPR_INFO,
            "[grpclb %p] lb_calld=%p: Status from LB server received. "
            "Status = %d, details = '%s', (lb_call: %p), error '%s'",
            grpclb_policy(), this, lb_call_status_, status_details, lb_call_,
            StatusToString(error).c_str());
    gpr_free(status_details);
  }
  // If this lb_calld is still in use, this call ended because of a failure so
  // we want to retry connecting. Otherwise, we have deliberately ended this
  // call and no further action is required.
  if (this == grpclb_policy()->lb_calld_.get()) {
    // If the fallback-at-startup checks are pending, go into fallback mode
    // immediately.  This short-circuits the timeout for the fallback-at-startup
    // case.
    grpclb_policy()->lb_calld_.reset();
    if (grpclb_policy()->fallback_at_startup_checks_pending_) {
      GPR_ASSERT(!seen_serverlist_);
      gpr_log(GPR_INFO,
              "[grpclb %p] Balancer call finished without receiving "
              "serverlist; entering fallback mode",
              grpclb_policy());
      grpclb_policy()->fallback_at_startup_checks_pending_ = false;
      grpclb_policy()->channel_control_helper()->GetEventEngine()->Cancel(
          *grpclb_policy()->lb_fallback_timer_handle_);
      grpclb_policy()->CancelBalancerChannelConnectivityWatchLocked();
      grpclb_policy()->fallback_mode_ = true;
      grpclb_policy()->CreateOrUpdateChildPolicyLocked();
    } else {
      // This handles the fallback-after-startup case.
      grpclb_policy()->MaybeEnterFallbackModeAfterStartup();
    }
    GPR_ASSERT(!grpclb_policy()->shutting_down_);
    grpclb_policy()->channel_control_helper()->RequestReresolution();
    if (seen_initial_response_) {
      // If we lose connection to the LB server, reset the backoff and restart
      // the LB call immediately.
      grpclb_policy()->lb_call_backoff_.Reset();
      grpclb_policy()->StartBalancerCallLocked();
    } else {
      // If this LB call fails establishing any connection to the LB server,
      // retry later.
      grpclb_policy()->StartBalancerCallRetryTimerLocked();
    }
  }
  Unref(DEBUG_LOCATION, "lb_call_ended");
}

//
// helper code for creating balancer channel
//

ServerAddressList ExtractBalancerAddresses(const ChannelArgs& args) {
  const ServerAddressList* addresses =
      FindGrpclbBalancerAddressesInChannelArgs(args);
  if (addresses != nullptr) return *addresses;
  return ServerAddressList();
}

// Returns the channel args for the LB channel, used to create a bidirectional
// stream for the reception of load balancing updates.
//
// Inputs:
//   - \a response_generator: in order to propagate updates from the resolver
//   above the grpclb policy.
//   - \a args: other args inherited from the grpclb policy.
ChannelArgs BuildBalancerChannelArgs(
    FakeResolverResponseGenerator* response_generator,
    const ChannelArgs& args) {
  ChannelArgs grpclb_channel_args;
  const grpc_channel_args* lb_channel_specific_args =
      args.GetPointer<grpc_channel_args>(
          GRPC_ARG_EXPERIMENTAL_GRPCLB_CHANNEL_ARGS);
  if (lb_channel_specific_args != nullptr) {
    grpclb_channel_args = ChannelArgs::FromC(lb_channel_specific_args);
  } else {
    // Set grpclb_channel_args based on the parent channel's channel args.
    grpclb_channel_args =
        args
            // LB policy name, since we want to use the default (pick_first) in
            // the LB channel.
            .Remove(GRPC_ARG_LB_POLICY_NAME)
            // Strip out the service config, since we don't want the LB policy
            // config specified for the parent channel to affect the LB channel.
            .Remove(GRPC_ARG_SERVICE_CONFIG)
            // The channel arg for the server URI, since that will be different
            // for the LB channel than for the parent channel.  The client
            // channel factory will re-add this arg with the right value.
            .Remove(GRPC_ARG_SERVER_URI)
            // The fake resolver response generator, because we are replacing it
            // with the one from the grpclb policy, used to propagate updates to
            // the LB channel.
            .Remove(GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR)
            // The LB channel should use the authority indicated by the target
            // authority table (see \a ModifyGrpclbBalancerChannelArgs),
            // as opposed to the authority from the parent channel.
            .Remove(GRPC_ARG_DEFAULT_AUTHORITY)
            // Just as for \a GRPC_ARG_DEFAULT_AUTHORITY, the LB channel should
            // be treated as a stand-alone channel and not inherit this argument
            // from the args of the parent channel.
            .Remove(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG)
            // Don't want to pass down channelz node from parent; the balancer
            // channel will get its own.
            .Remove(GRPC_ARG_CHANNELZ_CHANNEL_NODE)
            // Remove the channel args for channel credentials and replace it
            // with a version that does not contain call credentials. The
            // loadbalancer is not necessarily trusted to handle bearer token
            // credentials.
            .Remove(GRPC_ARG_CHANNEL_CREDENTIALS);
  }
  return grpclb_channel_args
      // A channel arg indicating the target is a grpclb load balancer.
      .Set(GRPC_ARG_ADDRESS_IS_GRPCLB_LOAD_BALANCER, 1)
      // Tells channelz that this is an internal channel.
      .Set(GRPC_ARG_CHANNELZ_IS_INTERNAL_CHANNEL, 1)
      // The fake resolver response generator, which we use to inject
      // address updates into the LB channel.
      .SetObject(response_generator->Ref());
}

//
// ctor and dtor
//

std::string GetServerNameFromChannelArgs(const ChannelArgs& args) {
  absl::StatusOr<URI> uri =
      URI::Parse(args.GetString(GRPC_ARG_SERVER_URI).value());
  GPR_ASSERT(uri.ok() && !uri->path().empty());
  return std::string(absl::StripPrefix(uri->path(), "/"));
}

GrpcLb::GrpcLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      server_name_(GetServerNameFromChannelArgs(channel_args())),
      response_generator_(MakeRefCounted<FakeResolverResponseGenerator>()),
      lb_call_timeout_(std::max(
          Duration::Zero(),
          channel_args()
              .GetDurationFromIntMillis(GRPC_ARG_GRPCLB_CALL_TIMEOUT_MS)
              .value_or(Duration::Zero()))),
      lb_call_backoff_(
          BackOff::Options()
              .set_initial_backoff(Duration::Seconds(
                  GRPC_GRPCLB_INITIAL_CONNECT_BACKOFF_SECONDS))
              .set_multiplier(GRPC_GRPCLB_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_GRPCLB_RECONNECT_JITTER)
              .set_max_backoff(Duration::Seconds(
                  GRPC_GRPCLB_RECONNECT_MAX_BACKOFF_SECONDS))),
      fallback_at_startup_timeout_(std::max(
          Duration::Zero(),
          channel_args()
              .GetDurationFromIntMillis(GRPC_ARG_GRPCLB_FALLBACK_TIMEOUT_MS)
              .value_or(Duration::Milliseconds(
                  GRPC_GRPCLB_DEFAULT_FALLBACK_TIMEOUT_MS)))),
      subchannel_cache_interval_(std::max(
          Duration::Zero(),
          channel_args()
              .GetDurationFromIntMillis(
                  GRPC_ARG_GRPCLB_SUBCHANNEL_CACHE_INTERVAL_MS)
              .value_or(Duration::Milliseconds(
                  GRPC_GRPCLB_DEFAULT_SUBCHANNEL_DELETION_DELAY_MS)))) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Will use '%s' as the server name for LB request.",
            this, server_name_.c_str());
  }
}

void GrpcLb::ShutdownLocked() {
  shutting_down_ = true;
  lb_calld_.reset();
  if (subchannel_cache_timer_handle_.has_value()) {
    channel_control_helper()->GetEventEngine()->Cancel(
        *subchannel_cache_timer_handle_);
    subchannel_cache_timer_handle_.reset();
  }
  cached_subchannels_.clear();
  if (lb_call_retry_timer_handle_.has_value()) {
    channel_control_helper()->GetEventEngine()->Cancel(
        *lb_call_retry_timer_handle_);
  }
  if (fallback_at_startup_checks_pending_) {
    fallback_at_startup_checks_pending_ = false;
    channel_control_helper()->GetEventEngine()->Cancel(
        *lb_fallback_timer_handle_);
    CancelBalancerChannelConnectivityWatchLocked();
  }
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
  // We destroy the LB channel here instead of in our destructor because
  // destroying the channel triggers a last callback to
  // OnBalancerChannelConnectivityChangedLocked(), and we need to be
  // alive when that callback is invoked.
  if (lb_channel_ != nullptr) {
    if (parent_channelz_node_ != nullptr) {
      channelz::ChannelNode* child_channelz_node =
          grpc_channel_get_channelz_node(lb_channel_);
      GPR_ASSERT(child_channelz_node != nullptr);
      parent_channelz_node_->RemoveChildChannel(child_channelz_node->uuid());
    }
    grpc_channel_destroy_internal(lb_channel_);
    lb_channel_ = nullptr;
  }
}

//
// public methods
//

void GrpcLb::ResetBackoffLocked() {
  if (lb_channel_ != nullptr) {
    grpc_channel_reset_connect_backoff(lb_channel_);
  }
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
  }
}

absl::Status GrpcLb::UpdateLocked(UpdateArgs args) {
  const bool is_initial_update = lb_channel_ == nullptr;
  config_ = args.config;
  GPR_ASSERT(config_ != nullptr);
  args_ = std::move(args.args);
  // Update fallback address list.
  fallback_backend_addresses_ = std::move(args.addresses);
  if (fallback_backend_addresses_.ok()) {
    // Add null LB token attributes.
    for (ServerAddress& address : *fallback_backend_addresses_) {
      address = ServerAddress(
          address.address(),
          address.args().SetObject(
              MakeRefCounted<TokenAndClientStatsArg>("", nullptr)));
    }
  }
  resolution_note_ = std::move(args.resolution_note);
  // Update balancer channel.
  absl::Status status = UpdateBalancerChannelLocked();
  // Update the existing child policy, if any.
  if (child_policy_ != nullptr) CreateOrUpdateChildPolicyLocked();
  // If this is the initial update, start the fallback-at-startup checks
  // and the balancer call.
  if (is_initial_update) {
    fallback_at_startup_checks_pending_ = true;
    // Start timer.
    lb_fallback_timer_handle_ =
        channel_control_helper()->GetEventEngine()->RunAfter(
            fallback_at_startup_timeout_,
            [self = static_cast<RefCountedPtr<GrpcLb>>(
                 Ref(DEBUG_LOCATION, "on_fallback_timer"))]() mutable {
              ApplicationCallbackExecCtx callback_exec_ctx;
              ExecCtx exec_ctx;
              auto self_ptr = self.get();
              self_ptr->work_serializer()->Run(
                  [self = std::move(self)]() { self->OnFallbackTimerLocked(); },
                  DEBUG_LOCATION);
            });
    // Start watching the channel's connectivity state.  If the channel
    // goes into state TRANSIENT_FAILURE before the timer fires, we go into
    // fallback mode even if the fallback timeout has not elapsed.
    ClientChannel* client_channel =
        ClientChannel::GetFromChannel(Channel::FromC(lb_channel_));
    GPR_ASSERT(client_channel != nullptr);
    // Ref held by callback.
    watcher_ = new StateWatcher(Ref(DEBUG_LOCATION, "StateWatcher"));
    client_channel->AddConnectivityWatcher(
        GRPC_CHANNEL_IDLE,
        OrphanablePtr<AsyncConnectivityStateWatcherInterface>(watcher_));
    // Start balancer call.
    StartBalancerCallLocked();
  }
  return status;
}

//
// helpers for UpdateLocked()
//

absl::Status GrpcLb::UpdateBalancerChannelLocked() {
  // Get balancer addresses.
  ServerAddressList balancer_addresses = ExtractBalancerAddresses(args_);
  absl::Status status;
  if (balancer_addresses.empty()) {
    status = absl::UnavailableError("balancer address list must be non-empty");
  }
  // Create channel credentials that do not contain call credentials.
  auto channel_credentials = channel_control_helper()->GetChannelCredentials();
  // Construct args for balancer channel.
  ChannelArgs lb_channel_args =
      BuildBalancerChannelArgs(response_generator_.get(), args_);
  // Create balancer channel if needed.
  if (lb_channel_ == nullptr) {
    std::string uri_str = absl::StrCat("fake:///", server_name_);
    lb_channel_ =
        grpc_channel_create(uri_str.c_str(), channel_credentials.get(),
                            lb_channel_args.ToC().get());
    GPR_ASSERT(lb_channel_ != nullptr);
    // Set up channelz linkage.
    channelz::ChannelNode* child_channelz_node =
        grpc_channel_get_channelz_node(lb_channel_);
    channelz::ChannelNode* parent_channelz_node =
        args_.GetObject<channelz::ChannelNode>();
    if (child_channelz_node != nullptr && parent_channelz_node != nullptr) {
      parent_channelz_node->AddChildChannel(child_channelz_node->uuid());
      parent_channelz_node_ = parent_channelz_node->Ref();
    }
  }
  // Propagate updates to the LB channel (pick_first) through the fake
  // resolver.
  Resolver::Result result;
  result.addresses = std::move(balancer_addresses);
  // Pass channel creds via channel args, since the fake resolver won't
  // do this automatically.
  result.args = lb_channel_args.SetObject(std::move(channel_credentials));
  response_generator_->SetResponse(std::move(result));
  // Return status.
  return status;
}

void GrpcLb::CancelBalancerChannelConnectivityWatchLocked() {
  ClientChannel* client_channel =
      ClientChannel::GetFromChannel(Channel::FromC(lb_channel_));
  GPR_ASSERT(client_channel != nullptr);
  client_channel->RemoveConnectivityWatcher(watcher_);
}

//
// code for balancer channel and call
//

void GrpcLb::StartBalancerCallLocked() {
  GPR_ASSERT(lb_channel_ != nullptr);
  if (shutting_down_) return;
  // Init the LB call data.
  GPR_ASSERT(lb_calld_ == nullptr);
  lb_calld_ = MakeOrphanable<BalancerCallState>(Ref());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Query for backends (lb_channel: %p, lb_calld: %p)",
            this, lb_channel_, lb_calld_.get());
  }
  lb_calld_->StartQuery();
}

void GrpcLb::StartBalancerCallRetryTimerLocked() {
  Duration timeout = lb_call_backoff_.NextAttemptTime() - Timestamp::Now();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO, "[grpclb %p] Connection to LB server lost...", this);
    if (timeout > Duration::Zero()) {
      gpr_log(GPR_INFO, "[grpclb %p] ... retry_timer_active in %" PRId64 "ms.",
              this, timeout.millis());
    } else {
      gpr_log(GPR_INFO, "[grpclb %p] ... retry_timer_active immediately.",
              this);
    }
  }
  lb_call_retry_timer_handle_ =
      channel_control_helper()->GetEventEngine()->RunAfter(
          timeout,
          [self = static_cast<RefCountedPtr<GrpcLb>>(
               Ref(DEBUG_LOCATION, "on_balancer_call_retry_timer"))]() mutable {
            ApplicationCallbackExecCtx callback_exec_ctx;
            ExecCtx exec_ctx;
            auto self_ptr = self.get();
            self_ptr->work_serializer()->Run(
                [self = std::move(self)]() {
                  self->OnBalancerCallRetryTimerLocked();
                },
                DEBUG_LOCATION);
          });
}

void GrpcLb::OnBalancerCallRetryTimerLocked() {
  lb_call_retry_timer_handle_.reset();
  if (!shutting_down_ && lb_calld_ == nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
      gpr_log(GPR_INFO, "[grpclb %p] Restarting call to LB server", this);
    }
    StartBalancerCallLocked();
  }
}

//
// code for handling fallback mode
//

void GrpcLb::MaybeEnterFallbackModeAfterStartup() {
  // Enter fallback mode if all of the following are true:
  // - We are not currently in fallback mode.
  // - We are not currently waiting for the initial fallback timeout.
  // - We are not currently in contact with the balancer.
  // - The child policy is not in state READY.
  if (!fallback_mode_ && !fallback_at_startup_checks_pending_ &&
      (lb_calld_ == nullptr || !lb_calld_->seen_serverlist()) &&
      !child_policy_ready_) {
    gpr_log(GPR_INFO,
            "[grpclb %p] lost contact with balancer and backends from "
            "most recent serverlist; entering fallback mode",
            this);
    fallback_mode_ = true;
    CreateOrUpdateChildPolicyLocked();
  }
}

void GrpcLb::OnFallbackTimerLocked() {
  // If we receive a serverlist after the timer fires but before this callback
  // actually runs, don't fall back.
  if (fallback_at_startup_checks_pending_ && !shutting_down_) {
    gpr_log(GPR_INFO,
            "[grpclb %p] No response from balancer after fallback timeout; "
            "entering fallback mode",
            this);
    fallback_at_startup_checks_pending_ = false;
    CancelBalancerChannelConnectivityWatchLocked();
    fallback_mode_ = true;
    CreateOrUpdateChildPolicyLocked();
  }
}

//
// code for interacting with the child policy
//

ChannelArgs GrpcLb::CreateChildPolicyArgsLocked(
    bool is_backend_from_grpclb_load_balancer) {
  ChannelArgs r =
      args_
          .Set(GRPC_ARG_ADDRESS_IS_BACKEND_FROM_GRPCLB_LOAD_BALANCER,
               is_backend_from_grpclb_load_balancer)
          .Set(GRPC_ARG_GRPCLB_ENABLE_LOAD_REPORTING_FILTER, 1);
  if (is_backend_from_grpclb_load_balancer) {
    r = r.Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, 1);
  }
  return r;
}

OrphanablePtr<LoadBalancingPolicy> GrpcLb::CreateChildPolicyLocked(
    const ChannelArgs& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_lb_glb_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO, "[grpclb %p] Created new child policy handler (%p)", this,
            lb_policy.get());
  }
  // Add the gRPC LB's interested_parties pollset_set to that of the newly
  // created child policy. This will make the child policy progress upon
  // activity on gRPC LB, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

void GrpcLb::CreateOrUpdateChildPolicyLocked() {
  if (shutting_down_) return;
  // Construct update args.
  UpdateArgs update_args;
  bool is_backend_from_grpclb_load_balancer = false;
  if (fallback_mode_) {
    // If CreateOrUpdateChildPolicyLocked() is invoked when we haven't
    // received any serverlist from the balancer, we use the fallback
    // backends returned by the resolver. Note that the fallback backend
    // list may be empty, in which case the new child policy will fail the
    // picks.
    update_args.addresses = fallback_backend_addresses_;
    if (fallback_backend_addresses_.ok() &&
        fallback_backend_addresses_->empty()) {
      update_args.resolution_note = absl::StrCat(
          "grpclb in fallback mode without any balancer addresses: ",
          resolution_note_);
    }
  } else {
    update_args.addresses = serverlist_->GetServerAddressList(
        lb_calld_ == nullptr ? nullptr : lb_calld_->client_stats());
    is_backend_from_grpclb_load_balancer = true;
  }
  update_args.args =
      CreateChildPolicyArgsLocked(is_backend_from_grpclb_load_balancer);
  GPR_ASSERT(update_args.args != ChannelArgs());
  update_args.config = config_->child_policy();
  // Create child policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(update_args.args);
  }
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO, "[grpclb %p] Updating child policy handler %p", this,
            child_policy_.get());
  }
  // TODO(roth): If we're in fallback mode and the child policy rejects the
  // update, we should propagate that failure back to the resolver somehow.
  (void)child_policy_->UpdateLocked(std::move(update_args));
}

//
// subchannel caching
//

void GrpcLb::CacheDeletedSubchannelLocked(
    RefCountedPtr<SubchannelInterface> subchannel) {
  Timestamp deletion_time = Timestamp::Now() + subchannel_cache_interval_;
  cached_subchannels_[deletion_time].push_back(std::move(subchannel));
  if (!subchannel_cache_timer_handle_.has_value()) {
    StartSubchannelCacheTimerLocked();
  }
}

void GrpcLb::StartSubchannelCacheTimerLocked() {
  GPR_ASSERT(!cached_subchannels_.empty());
  subchannel_cache_timer_handle_ =
      channel_control_helper()->GetEventEngine()->RunAfter(
          cached_subchannels_.begin()->first - Timestamp::Now(),
          [self = static_cast<RefCountedPtr<GrpcLb>>(
               Ref(DEBUG_LOCATION, "OnSubchannelCacheTimer"))]() mutable {
            ApplicationCallbackExecCtx callback_exec_ctx;
            ExecCtx exec_ctx;
            auto* self_ptr = self.get();
            self_ptr->work_serializer()->Run(
                [self = std::move(self)]() mutable {
                  self->OnSubchannelCacheTimerLocked();
                },
                DEBUG_LOCATION);
          });
}

void GrpcLb::OnSubchannelCacheTimerLocked() {
  if (subchannel_cache_timer_handle_.has_value()) {
    subchannel_cache_timer_handle_.reset();
    auto it = cached_subchannels_.begin();
    if (it != cached_subchannels_.end()) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
        gpr_log(GPR_INFO,
                "[grpclb %p] removing %" PRIuPTR " subchannels from cache",
                this, it->second.size());
      }
      cached_subchannels_.erase(it);
    }
    if (!cached_subchannels_.empty()) {
      StartSubchannelCacheTimerLocked();
      return;
    }
  }
}

//
// factory
//

class GrpcLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<GrpcLb>(std::move(args));
  }

  absl::string_view name() const override { return kGrpclb; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadFromJson<RefCountedPtr<GrpcLbConfig>>(
        json, JsonArgs(), "errors validating grpclb LB policy config");
  }
};

}  // namespace

//
// Plugin registration
//

void RegisterGrpcLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<GrpcLbFactory>());
  builder->channel_init()->RegisterStage(
      GRPC_CLIENT_SUBCHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](ChannelStackBuilder* builder) {
        auto enable = builder->channel_args().GetBool(
            GRPC_ARG_GRPCLB_ENABLE_LOAD_REPORTING_FILTER);
        if (enable.has_value() && *enable) {
          builder->PrependFilter(&ClientLoadReportingFilter::kFilter);
        }
        return true;
      });
}

}  // namespace grpc_core
