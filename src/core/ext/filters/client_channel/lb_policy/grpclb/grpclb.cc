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

// With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
// using that endpoint. Because of various transitive includes in uv.h,
// including windows.h on Windows, uv.h must be included before other system
// headers. Therefore, sockaddr.h must always be included first.
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_hash_table.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/static_metadata.h"

#define GRPC_GRPCLB_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_GRPCLB_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_GRPCLB_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_GRPCLB_RECONNECT_JITTER 0.2
#define GRPC_GRPCLB_DEFAULT_FALLBACK_TIMEOUT_MS 10000

#define GRPC_ARG_GRPCLB_ADDRESS_LB_TOKEN "grpc.grpclb_address_lb_token"

namespace grpc_core {

TraceFlag grpc_lb_glb_trace(false, "glb");

namespace {

constexpr char kGrpclb[] = "grpclb";

class ParsedGrpcLbConfig : public LoadBalancingPolicy::Config {
 public:
  explicit ParsedGrpcLbConfig(
      RefCountedPtr<LoadBalancingPolicy::Config> child_policy)
      : child_policy_(std::move(child_policy)) {}
  const char* name() const override { return kGrpclb; }

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }

 private:
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
};

class GrpcLb : public LoadBalancingPolicy {
 public:
  explicit GrpcLb(Args args);

  const char* name() const override { return kGrpclb; }

  void UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  /// Contains a call to the LB server and all the data related to the call.
  class BalancerCallState : public InternallyRefCounted<BalancerCallState> {
   public:
    explicit BalancerCallState(
        RefCountedPtr<LoadBalancingPolicy> parent_grpclb_policy);

    // It's the caller's responsibility to ensure that Orphan() is called from
    // inside the combiner.
    void Orphan() override;

    void StartQuery();

    GrpcLbClientStats* client_stats() const { return client_stats_.get(); }

    bool seen_initial_response() const { return seen_initial_response_; }
    bool seen_serverlist() const { return seen_serverlist_; }

   private:
    // So Delete() can access our private dtor.
    template <typename T>
    friend void grpc_core::Delete(T*);

    ~BalancerCallState();

    GrpcLb* grpclb_policy() const {
      return static_cast<GrpcLb*>(grpclb_policy_.get());
    }

    void ScheduleNextClientLoadReportLocked();
    void SendClientLoadReportLocked();

    static bool LoadReportCountersAreZero(grpc_grpclb_request* request);

    static void MaybeSendClientLoadReportLocked(void* arg, grpc_error* error);
    static void ClientLoadReportDoneLocked(void* arg, grpc_error* error);
    static void OnInitialRequestSentLocked(void* arg, grpc_error* error);
    static void OnBalancerMessageReceivedLocked(void* arg, grpc_error* error);
    static void OnBalancerStatusReceivedLocked(void* arg, grpc_error* error);

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
    grpc_millis client_stats_report_interval_ = 0;
    grpc_timer client_load_report_timer_;
    bool client_load_report_timer_callback_pending_ = false;
    bool last_client_load_report_counters_were_zero_ = false;
    bool client_load_report_is_due_ = false;
    // The closure used for either the load report timer or the callback for
    // completion of sending the load report.
    grpc_closure client_load_report_closure_;
  };

  class Serverlist : public RefCounted<Serverlist> {
   public:
    // Takes ownership of serverlist.
    explicit Serverlist(grpc_grpclb_serverlist* serverlist)
        : serverlist_(serverlist) {}

    ~Serverlist() { grpc_grpclb_destroy_serverlist(serverlist_); }

    bool operator==(const Serverlist& other) const;

    const grpc_grpclb_serverlist* serverlist() const { return serverlist_; }

    // Returns a text representation suitable for logging.
    UniquePtr<char> AsText() const;

    // Extracts all non-drop entries into a ServerAddressList.
    ServerAddressList GetServerAddressList(
        GrpcLbClientStats* client_stats) const;

    // Returns true if the serverlist contains at least one drop entry and
    // no backend address entries.
    bool ContainsAllDropEntries() const;

    // Returns the LB token to use for a drop, or null if the call
    // should not be dropped.
    //
    // Note: This is called from the picker, so it will be invoked in
    // the channel's data plane combiner, NOT the control plane
    // combiner.  It should not be accessed by any other part of the LB
    // policy.
    const char* ShouldDrop();

   private:
    grpc_grpclb_serverlist* serverlist_;

    // Guarded by the channel's data plane combiner, NOT the control
    // plane combiner.  It should not be accessed by anything but the
    // picker via the ShouldDrop() method.
    size_t drop_index_ = 0;
  };

  class Picker : public SubchannelPicker {
   public:
    Picker(GrpcLb* parent, RefCountedPtr<Serverlist> serverlist,
           UniquePtr<SubchannelPicker> child_picker,
           RefCountedPtr<GrpcLbClientStats> client_stats)
        : parent_(parent),
          serverlist_(std::move(serverlist)),
          child_picker_(std::move(child_picker)),
          client_stats_(std::move(client_stats)) {}

    PickResult Pick(PickArgs args) override;

   private:
    // Storing the address for logging, but not holding a ref.
    // DO NOT DEFERENCE!
    GrpcLb* parent_;

    // Serverlist to be used for determining drops.
    RefCountedPtr<Serverlist> serverlist_;

    UniquePtr<SubchannelPicker> child_picker_;
    RefCountedPtr<GrpcLbClientStats> client_stats_;
  };

  class Helper : public ChannelControlHelper {
   public:
    explicit Helper(RefCountedPtr<GrpcLb> parent)
        : parent_(std::move(parent)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_channel_args& args) override;
    grpc_channel* CreateChannel(const char* target,
                                const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state,
                     UniquePtr<SubchannelPicker> picker) override;
    void RequestReresolution() override;
    void AddTraceEvent(TraceSeverity severity, const char* message) override;

    void set_child(LoadBalancingPolicy* child) { child_ = child; }

   private:
    bool CalledByPendingChild() const;
    bool CalledByCurrentChild() const;

    RefCountedPtr<GrpcLb> parent_;
    LoadBalancingPolicy* child_ = nullptr;
  };

  ~GrpcLb();

  void ShutdownLocked() override;

  // Helper functions used in UpdateLocked().
  void ProcessAddressesAndChannelArgsLocked(const ServerAddressList& addresses,
                                            const grpc_channel_args& args);
  static void OnBalancerChannelConnectivityChangedLocked(void* arg,
                                                         grpc_error* error);
  void CancelBalancerChannelConnectivityWatchLocked();

  // Methods for dealing with fallback state.
  void MaybeEnterFallbackModeAfterStartup();
  static void OnFallbackTimerLocked(void* arg, grpc_error* error);

  // Methods for dealing with the balancer call.
  void StartBalancerCallLocked();
  void StartBalancerCallRetryTimerLocked();
  static void OnBalancerCallRetryTimerLocked(void* arg, grpc_error* error);

  // Methods for dealing with the child policy.
  grpc_channel_args* CreateChildPolicyArgsLocked(
      bool is_backend_from_grpclb_load_balancer);
  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const char* name, const grpc_channel_args* args);
  void CreateOrUpdateChildPolicyLocked();

  // Who the client is trying to communicate with.
  const char* server_name_ = nullptr;

  // Current channel args from the resolver.
  grpc_channel_args* args_ = nullptr;

  // Internal state.
  bool shutting_down_ = false;

  // The channel for communicating with the LB server.
  grpc_channel* lb_channel_ = nullptr;
  // Response generator to inject address updates into lb_channel_.
  RefCountedPtr<FakeResolverResponseGenerator> response_generator_;

  // The data associated with the current LB call. It holds a ref to this LB
  // policy. It's initialized every time we query for backends. It's reset to
  // NULL whenever the current LB call is no longer needed (e.g., the LB policy
  // is shutting down, or the LB call has ended). A non-NULL lb_calld_ always
  // contains a non-NULL lb_call_.
  OrphanablePtr<BalancerCallState> lb_calld_;
  // Timeout in milliseconds for the LB call. 0 means no deadline.
  int lb_call_timeout_ms_ = 0;
  // Balancer call retry state.
  BackOff lb_call_backoff_;
  bool retry_timer_callback_pending_ = false;
  grpc_timer lb_call_retry_timer_;
  grpc_closure lb_on_call_retry_;

  // The deserialized response from the balancer. May be nullptr until one
  // such response has arrived.
  RefCountedPtr<Serverlist> serverlist_;

  // Whether we're in fallback mode.
  bool fallback_mode_ = false;
  // The backend addresses from the resolver.
  ServerAddressList fallback_backend_addresses_;
  // State for fallback-at-startup checks.
  // Timeout after startup after which we will go into fallback mode if
  // we have not received a serverlist from the balancer.
  int fallback_at_startup_timeout_ = 0;
  bool fallback_at_startup_checks_pending_ = false;
  grpc_timer lb_fallback_timer_;
  grpc_closure lb_on_fallback_;
  grpc_connectivity_state lb_channel_connectivity_ = GRPC_CHANNEL_IDLE;
  grpc_closure lb_channel_on_connectivity_changed_;

  // The child policy to use for the backends.
  OrphanablePtr<LoadBalancingPolicy> child_policy_;
  // When switching child policies, the new policy will be stored here
  // until it reports READY, at which point it will be moved to child_policy_.
  OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;
  // The child policy config.
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_config_;
  // Child policy in state READY.
  bool child_policy_ready_ = false;
};

//
// GrpcLb::Serverlist
//

bool GrpcLb::Serverlist::operator==(const Serverlist& other) const {
  return grpc_grpclb_serverlist_equals(serverlist_, other.serverlist_);
}

void ParseServer(const grpc_grpclb_server* server,
                 grpc_resolved_address* addr) {
  memset(addr, 0, sizeof(*addr));
  if (server->drop) return;
  const uint16_t netorder_port = grpc_htons((uint16_t)server->port);
  /* the addresses are given in binary format (a in(6)_addr struct) in
   * server->ip_address.bytes. */
  const grpc_grpclb_ip_address* ip = &server->ip_address;
  if (ip->size == 4) {
    addr->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in));
    grpc_sockaddr_in* addr4 = reinterpret_cast<grpc_sockaddr_in*>(&addr->addr);
    addr4->sin_family = GRPC_AF_INET;
    memcpy(&addr4->sin_addr, ip->bytes, ip->size);
    addr4->sin_port = netorder_port;
  } else if (ip->size == 16) {
    addr->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in6));
    grpc_sockaddr_in6* addr6 = (grpc_sockaddr_in6*)&addr->addr;
    addr6->sin6_family = GRPC_AF_INET6;
    memcpy(&addr6->sin6_addr, ip->bytes, ip->size);
    addr6->sin6_port = netorder_port;
  }
}

UniquePtr<char> GrpcLb::Serverlist::AsText() const {
  gpr_strvec entries;
  gpr_strvec_init(&entries);
  for (size_t i = 0; i < serverlist_->num_servers; ++i) {
    const auto* server = serverlist_->servers[i];
    char* ipport;
    if (server->drop) {
      ipport = gpr_strdup("(drop)");
    } else {
      grpc_resolved_address addr;
      ParseServer(server, &addr);
      grpc_sockaddr_to_string(&ipport, &addr, false);
    }
    char* entry;
    gpr_asprintf(&entry, "  %" PRIuPTR ": %s token=%s\n", i, ipport,
                 server->load_balance_token);
    gpr_free(ipport);
    gpr_strvec_add(&entries, entry);
  }
  UniquePtr<char> result(gpr_strvec_flatten(&entries, nullptr));
  gpr_strvec_destroy(&entries);
  return result;
}

// vtable for LB token channel arg.
void* lb_token_copy(void* token) {
  return token == nullptr
             ? nullptr
             : (void*)GRPC_MDELEM_REF(grpc_mdelem{(uintptr_t)token}).payload;
}
void lb_token_destroy(void* token) {
  if (token != nullptr) {
    GRPC_MDELEM_UNREF(grpc_mdelem{(uintptr_t)token});
  }
}
int lb_token_cmp(void* token1, void* token2) {
  // Always indicate a match, since we don't want this channel arg to
  // affect the subchannel's key in the index.
  return 0;
}
const grpc_arg_pointer_vtable lb_token_arg_vtable = {
    lb_token_copy, lb_token_destroy, lb_token_cmp};

bool IsServerValid(const grpc_grpclb_server* server, size_t idx, bool log) {
  if (server->drop) return false;
  const grpc_grpclb_ip_address* ip = &server->ip_address;
  if (GPR_UNLIKELY(server->port >> 16 != 0)) {
    if (log) {
      gpr_log(GPR_ERROR,
              "Invalid port '%d' at index %lu of serverlist. Ignoring.",
              server->port, (unsigned long)idx);
    }
    return false;
  }
  if (GPR_UNLIKELY(ip->size != 4 && ip->size != 16)) {
    if (log) {
      gpr_log(GPR_ERROR,
              "Expected IP to be 4 or 16 bytes, got %d at index %lu of "
              "serverlist. Ignoring",
              ip->size, (unsigned long)idx);
    }
    return false;
  }
  return true;
}

// Returns addresses extracted from the serverlist.
ServerAddressList GrpcLb::Serverlist::GetServerAddressList(
    GrpcLbClientStats* client_stats) const {
  ServerAddressList addresses;
  for (size_t i = 0; i < serverlist_->num_servers; ++i) {
    const grpc_grpclb_server* server = serverlist_->servers[i];
    if (!IsServerValid(serverlist_->servers[i], i, false)) continue;
    // Address processing.
    grpc_resolved_address addr;
    ParseServer(server, &addr);
    // LB token processing.
    grpc_mdelem lb_token;
    if (server->has_load_balance_token) {
      const size_t lb_token_max_length =
          GPR_ARRAY_SIZE(server->load_balance_token);
      const size_t lb_token_length =
          strnlen(server->load_balance_token, lb_token_max_length);
      grpc_slice lb_token_mdstr = grpc_slice_from_copied_buffer(
          server->load_balance_token, lb_token_length);
      lb_token = grpc_mdelem_from_slices(GRPC_MDSTR_LB_TOKEN, lb_token_mdstr);
      if (client_stats != nullptr) {
        GPR_ASSERT(grpc_mdelem_set_user_data(
                       lb_token, GrpcLbClientStats::Destroy,
                       client_stats->Ref().release()) == client_stats);
      }
    } else {
      char* uri = grpc_sockaddr_to_uri(&addr);
      gpr_log(GPR_INFO,
              "Missing LB token for backend address '%s'. The empty token will "
              "be used instead",
              uri);
      gpr_free(uri);
      lb_token = GRPC_MDELEM_LB_TOKEN_EMPTY;
    }
    // Add address.
    grpc_arg arg = grpc_channel_arg_pointer_create(
        const_cast<char*>(GRPC_ARG_GRPCLB_ADDRESS_LB_TOKEN),
        (void*)lb_token.payload, &lb_token_arg_vtable);
    grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
    addresses.emplace_back(addr, args);
    // Clean up.
    GRPC_MDELEM_UNREF(lb_token);
  }
  return addresses;
}

bool GrpcLb::Serverlist::ContainsAllDropEntries() const {
  if (serverlist_->num_servers == 0) return false;
  for (size_t i = 0; i < serverlist_->num_servers; ++i) {
    if (!serverlist_->servers[i]->drop) return false;
  }
  return true;
}

const char* GrpcLb::Serverlist::ShouldDrop() {
  if (serverlist_->num_servers == 0) return nullptr;
  grpc_grpclb_server* server = serverlist_->servers[drop_index_];
  drop_index_ = (drop_index_ + 1) % serverlist_->num_servers;
  return server->drop ? server->load_balance_token : nullptr;
}

//
// GrpcLb::Picker
//

GrpcLb::PickResult GrpcLb::Picker::Pick(PickArgs args) {
  PickResult result;
  // Check if we should drop the call.
  const char* drop_token = serverlist_->ShouldDrop();
  if (drop_token != nullptr) {
    // Update client load reporting stats to indicate the number of
    // dropped calls.  Note that we have to do this here instead of in
    // the client_load_reporting filter, because we do not create a
    // subchannel call (and therefore no client_load_reporting filter)
    // for dropped calls.
    if (client_stats_ != nullptr) {
      client_stats_->AddCallDropped(drop_token);
    }
    result.type = PickResult::PICK_COMPLETE;
    return result;
  }
  // Forward pick to child policy.
  result = child_picker_->Pick(args);
  // If pick succeeded, add LB token to initial metadata.
  if (result.type == PickResult::PICK_COMPLETE &&
      result.connected_subchannel != nullptr) {
    const grpc_arg* arg = grpc_channel_args_find(
        result.connected_subchannel->args(), GRPC_ARG_GRPCLB_ADDRESS_LB_TOKEN);
    if (arg == nullptr) {
      gpr_log(GPR_ERROR,
              "[grpclb %p picker %p] No LB token for connected subchannel %p",
              parent_, this, result.connected_subchannel.get());
      abort();
    }
    grpc_mdelem lb_token = {reinterpret_cast<uintptr_t>(arg->value.pointer.p)};
    GPR_ASSERT(!GRPC_MDISNULL(lb_token));
    grpc_linked_mdelem* mdelem_storage = static_cast<grpc_linked_mdelem*>(
        args.call_state->Alloc(sizeof(grpc_linked_mdelem)));
    GPR_ASSERT(grpc_metadata_batch_add_tail(
                   args.initial_metadata, mdelem_storage,
                   GRPC_MDELEM_REF(lb_token)) == GRPC_ERROR_NONE);
    GrpcLbClientStats* client_stats = static_cast<GrpcLbClientStats*>(
        grpc_mdelem_get_user_data(lb_token, GrpcLbClientStats::Destroy));
    if (client_stats != nullptr) {
      client_stats->AddCallStarted();
    }
  }
  return result;
}

//
// GrpcLb::Helper
//

bool GrpcLb::Helper::CalledByPendingChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == parent_->pending_child_policy_.get();
}

bool GrpcLb::Helper::CalledByCurrentChild() const {
  GPR_ASSERT(child_ != nullptr);
  return child_ == parent_->child_policy_.get();
}

RefCountedPtr<SubchannelInterface> GrpcLb::Helper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return parent_->channel_control_helper()->CreateSubchannel(args);
}

grpc_channel* GrpcLb::Helper::CreateChannel(const char* target,
                                            const grpc_channel_args& args) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return parent_->channel_control_helper()->CreateChannel(target, args);
}

void GrpcLb::Helper::UpdateState(grpc_connectivity_state state,
                                 UniquePtr<SubchannelPicker> picker) {
  if (parent_->shutting_down_) return;
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
      gpr_log(GPR_INFO,
              "[grpclb %p helper %p] pending child policy %p reports state=%s",
              parent_.get(), this, parent_->pending_child_policy_.get(),
              grpc_connectivity_state_name(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    grpc_pollset_set_del_pollset_set(
        parent_->child_policy_->interested_parties(),
        parent_->interested_parties());
    parent_->child_policy_ = std::move(parent_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdated child, so ignore it.
    return;
  }
  // Record whether child policy reports READY.
  parent_->child_policy_ready_ = state == GRPC_CHANNEL_READY;
  // Enter fallback mode if needed.
  parent_->MaybeEnterFallbackModeAfterStartup();
  // There are three cases to consider here:
  // 1. We're in fallback mode.  In this case, we're always going to use
  //    the child policy's result, so we pass its picker through as-is.
  // 2. The serverlist contains only drop entries.  In this case, we
  //    want to use our own picker so that we can return the drops.
  // 3. Not in fallback mode and serverlist is not all drops (i.e., it
  //    may be empty or contain at least one backend address).  There are
  //    two sub-cases:
  //    a. The child policy is reporting state READY.  In this case, we wrap
  //       the child's picker in our own, so that we can handle drops and LB
  //       token metadata for each pick.
  //    b. The child policy is reporting a state other than READY.  In this
  //       case, we don't want to use our own picker, because we don't want
  //       to process drops for picks that yield a QUEUE result; this would
  //       result in dropping too many calls, since we will see the
  //       queued picks multiple times, and we'd consider each one a
  //       separate call for the drop calculation.
  //
  // Cases 1 and 3b: return picker from the child policy as-is.
  if (parent_->serverlist_ == nullptr ||
      (!parent_->serverlist_->ContainsAllDropEntries() &&
       state != GRPC_CHANNEL_READY)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
      gpr_log(GPR_INFO,
              "[grpclb %p helper %p] state=%s passing child picker %p as-is",
              parent_.get(), this, grpc_connectivity_state_name(state),
              picker.get());
    }
    parent_->channel_control_helper()->UpdateState(state, std::move(picker));
    return;
  }
  // Cases 2 and 3a: wrap picker from the child in our own picker.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO, "[grpclb %p helper %p] state=%s wrapping child picker %p",
            parent_.get(), this, grpc_connectivity_state_name(state),
            picker.get());
  }
  RefCountedPtr<GrpcLbClientStats> client_stats;
  if (parent_->lb_calld_ != nullptr &&
      parent_->lb_calld_->client_stats() != nullptr) {
    client_stats = parent_->lb_calld_->client_stats()->Ref();
  }
  parent_->channel_control_helper()->UpdateState(
      state, UniquePtr<SubchannelPicker>(
                 New<Picker>(parent_.get(), parent_->serverlist_,
                             std::move(picker), std::move(client_stats))));
}

void GrpcLb::Helper::RequestReresolution() {
  if (parent_->shutting_down_) return;
  const LoadBalancingPolicy* latest_child_policy =
      parent_->pending_child_policy_ != nullptr
          ? parent_->pending_child_policy_.get()
          : parent_->child_policy_.get();
  if (child_ != latest_child_policy) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Re-resolution requested from %schild policy (%p).",
            parent_.get(), CalledByPendingChild() ? "pending " : "", child_);
  }
  // If we are talking to a balancer, we expect to get updated addresses
  // from the balancer, so we can ignore the re-resolution request from
  // the child policy. Otherwise, pass the re-resolution request up to the
  // channel.
  if (parent_->lb_calld_ == nullptr ||
      !parent_->lb_calld_->seen_initial_response()) {
    parent_->channel_control_helper()->RequestReresolution();
  }
}

void GrpcLb::Helper::AddTraceEvent(TraceSeverity severity,
                                   const char* message) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return;
  }
  parent_->channel_control_helper()->AddTraceEvent(severity, message);
}

//
// GrpcLb::BalancerCallState
//

GrpcLb::BalancerCallState::BalancerCallState(
    RefCountedPtr<LoadBalancingPolicy> parent_grpclb_policy)
    : InternallyRefCounted<BalancerCallState>(&grpc_lb_glb_trace),
      grpclb_policy_(std::move(parent_grpclb_policy)) {
  GPR_ASSERT(grpclb_policy_ != nullptr);
  GPR_ASSERT(!grpclb_policy()->shutting_down_);
  // Init the LB call. Note that the LB call will progress every time there's
  // activity in grpclb_policy_->interested_parties(), which is comprised of
  // the polling entities from client_channel.
  GPR_ASSERT(grpclb_policy()->server_name_ != nullptr);
  GPR_ASSERT(grpclb_policy()->server_name_[0] != '\0');
  const grpc_millis deadline =
      grpclb_policy()->lb_call_timeout_ms_ == 0
          ? GRPC_MILLIS_INF_FUTURE
          : ExecCtx::Get()->Now() + grpclb_policy()->lb_call_timeout_ms_;
  lb_call_ = grpc_channel_create_pollset_set_call(
      grpclb_policy()->lb_channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      grpclb_policy_->interested_parties(),
      GRPC_MDSTR_SLASH_GRPC_DOT_LB_DOT_V1_DOT_LOADBALANCER_SLASH_BALANCELOAD,
      nullptr, deadline, nullptr);
  // Init the LB call request payload.
  grpc_grpclb_request* request =
      grpc_grpclb_request_create(grpclb_policy()->server_name_);
  grpc_slice request_payload_slice = grpc_grpclb_request_encode(request);
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  grpc_grpclb_request_destroy(request);
  // Init other data associated with the LB call.
  grpc_metadata_array_init(&lb_initial_metadata_recv_);
  grpc_metadata_array_init(&lb_trailing_metadata_recv_);
  GRPC_CLOSURE_INIT(&lb_on_initial_request_sent_, OnInitialRequestSentLocked,
                    this, grpc_combiner_scheduler(grpclb_policy()->combiner()));
  GRPC_CLOSURE_INIT(&lb_on_balancer_message_received_,
                    OnBalancerMessageReceivedLocked, this,
                    grpc_combiner_scheduler(grpclb_policy()->combiner()));
  GRPC_CLOSURE_INIT(&lb_on_balancer_status_received_,
                    OnBalancerStatusReceivedLocked, this,
                    grpc_combiner_scheduler(grpclb_policy()->combiner()));
}

GrpcLb::BalancerCallState::~BalancerCallState() {
  GPR_ASSERT(lb_call_ != nullptr);
  grpc_call_unref(lb_call_);
  grpc_metadata_array_destroy(&lb_initial_metadata_recv_);
  grpc_metadata_array_destroy(&lb_trailing_metadata_recv_);
  grpc_byte_buffer_destroy(send_message_payload_);
  grpc_byte_buffer_destroy(recv_message_payload_);
  grpc_slice_unref_internal(lb_call_status_details_);
}

void GrpcLb::BalancerCallState::Orphan() {
  GPR_ASSERT(lb_call_ != nullptr);
  // If we are here because grpclb_policy wants to cancel the call,
  // lb_on_balancer_status_received_ will complete the cancellation and clean
  // up. Otherwise, we are here because grpclb_policy has to orphan a failed
  // call, then the following cancellation will be a no-op.
  grpc_call_cancel(lb_call_, nullptr);
  if (client_load_report_timer_callback_pending_) {
    grpc_timer_cancel(&client_load_report_timer_);
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
  call_error = grpc_call_start_batch_and_execute(
      lb_call_, ops, (size_t)(op - ops), &lb_on_initial_request_sent_);
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
      lb_call_, ops, (size_t)(op - ops), &lb_on_balancer_message_received_);
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
      lb_call_, ops, (size_t)(op - ops), &lb_on_balancer_status_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void GrpcLb::BalancerCallState::ScheduleNextClientLoadReportLocked() {
  const grpc_millis next_client_load_report_time =
      ExecCtx::Get()->Now() + client_stats_report_interval_;
  GRPC_CLOSURE_INIT(&client_load_report_closure_,
                    MaybeSendClientLoadReportLocked, this,
                    grpc_combiner_scheduler(grpclb_policy()->combiner()));
  grpc_timer_init(&client_load_report_timer_, next_client_load_report_time,
                  &client_load_report_closure_);
  client_load_report_timer_callback_pending_ = true;
}

void GrpcLb::BalancerCallState::MaybeSendClientLoadReportLocked(
    void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  GrpcLb* grpclb_policy = lb_calld->grpclb_policy();
  lb_calld->client_load_report_timer_callback_pending_ = false;
  if (error != GRPC_ERROR_NONE || lb_calld != grpclb_policy->lb_calld_.get()) {
    lb_calld->Unref(DEBUG_LOCATION, "client_load_report");
    return;
  }
  // If we've already sent the initial request, then we can go ahead and send
  // the load report. Otherwise, we need to wait until the initial request has
  // been sent to send this (see OnInitialRequestSentLocked()).
  if (lb_calld->send_message_payload_ == nullptr) {
    lb_calld->SendClientLoadReportLocked();
  } else {
    lb_calld->client_load_report_is_due_ = true;
  }
}

bool GrpcLb::BalancerCallState::LoadReportCountersAreZero(
    grpc_grpclb_request* request) {
  GrpcLbClientStats::DroppedCallCounts* drop_entries =
      static_cast<GrpcLbClientStats::DroppedCallCounts*>(
          request->client_stats.calls_finished_with_drop.arg);
  return request->client_stats.num_calls_started == 0 &&
         request->client_stats.num_calls_finished == 0 &&
         request->client_stats.num_calls_finished_with_client_failed_to_send ==
             0 &&
         request->client_stats.num_calls_finished_known_received == 0 &&
         (drop_entries == nullptr || drop_entries->size() == 0);
}

void GrpcLb::BalancerCallState::SendClientLoadReportLocked() {
  // Construct message payload.
  GPR_ASSERT(send_message_payload_ == nullptr);
  grpc_grpclb_request* request =
      grpc_grpclb_load_report_request_create(client_stats_.get());
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  if (LoadReportCountersAreZero(request)) {
    if (last_client_load_report_counters_were_zero_) {
      grpc_grpclb_request_destroy(request);
      ScheduleNextClientLoadReportLocked();
      return;
    }
    last_client_load_report_counters_were_zero_ = true;
  } else {
    last_client_load_report_counters_were_zero_ = false;
  }
  grpc_slice request_payload_slice = grpc_grpclb_request_encode(request);
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  grpc_grpclb_request_destroy(request);
  // Send the report.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message = send_message_payload_;
  GRPC_CLOSURE_INIT(&client_load_report_closure_, ClientLoadReportDoneLocked,
                    this, grpc_combiner_scheduler(grpclb_policy()->combiner()));
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      lb_call_, &op, 1, &client_load_report_closure_);
  if (GPR_UNLIKELY(call_error != GRPC_CALL_OK)) {
    gpr_log(GPR_ERROR,
            "[grpclb %p] lb_calld=%p call_error=%d sending client load report",
            grpclb_policy_.get(), this, call_error);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  }
}

void GrpcLb::BalancerCallState::ClientLoadReportDoneLocked(void* arg,
                                                           grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  GrpcLb* grpclb_policy = lb_calld->grpclb_policy();
  grpc_byte_buffer_destroy(lb_calld->send_message_payload_);
  lb_calld->send_message_payload_ = nullptr;
  if (error != GRPC_ERROR_NONE || lb_calld != grpclb_policy->lb_calld_.get()) {
    lb_calld->Unref(DEBUG_LOCATION, "client_load_report");
    return;
  }
  lb_calld->ScheduleNextClientLoadReportLocked();
}

void GrpcLb::BalancerCallState::OnInitialRequestSentLocked(void* arg,
                                                           grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  grpc_byte_buffer_destroy(lb_calld->send_message_payload_);
  lb_calld->send_message_payload_ = nullptr;
  // If we attempted to send a client load report before the initial request was
  // sent (and this lb_calld is still in use), send the load report now.
  if (lb_calld->client_load_report_is_due_ &&
      lb_calld == lb_calld->grpclb_policy()->lb_calld_.get()) {
    lb_calld->SendClientLoadReportLocked();
    lb_calld->client_load_report_is_due_ = false;
  }
  lb_calld->Unref(DEBUG_LOCATION, "on_initial_request_sent");
}

void GrpcLb::BalancerCallState::OnBalancerMessageReceivedLocked(
    void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  GrpcLb* grpclb_policy = lb_calld->grpclb_policy();
  // Null payload means the LB call was cancelled.
  if (lb_calld != grpclb_policy->lb_calld_.get() ||
      lb_calld->recv_message_payload_ == nullptr) {
    lb_calld->Unref(DEBUG_LOCATION, "on_message_received");
    return;
  }
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, lb_calld->recv_message_payload_);
  grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_byte_buffer_reader_destroy(&bbr);
  grpc_byte_buffer_destroy(lb_calld->recv_message_payload_);
  lb_calld->recv_message_payload_ = nullptr;
  grpc_grpclb_initial_response* initial_response;
  grpc_grpclb_serverlist* serverlist;
  if (!lb_calld->seen_initial_response_ &&
      (initial_response = grpc_grpclb_initial_response_parse(response_slice)) !=
          nullptr) {
    // Have NOT seen initial response, look for initial response.
    if (initial_response->has_client_stats_report_interval) {
      lb_calld->client_stats_report_interval_ = GPR_MAX(
          GPR_MS_PER_SEC, grpc_grpclb_duration_to_millis(
                              &initial_response->client_stats_report_interval));
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
        gpr_log(GPR_INFO,
                "[grpclb %p] lb_calld=%p: Received initial LB response "
                "message; client load reporting interval = %" PRId64
                " milliseconds",
                grpclb_policy, lb_calld,
                lb_calld->client_stats_report_interval_);
      }
    } else if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
      gpr_log(GPR_INFO,
              "[grpclb %p] lb_calld=%p: Received initial LB response message; "
              "client load reporting NOT enabled",
              grpclb_policy, lb_calld);
    }
    grpc_grpclb_initial_response_destroy(initial_response);
    lb_calld->seen_initial_response_ = true;
  } else if ((serverlist = grpc_grpclb_response_parse_serverlist(
                  response_slice)) != nullptr) {
    // Have seen initial response, look for serverlist.
    GPR_ASSERT(lb_calld->lb_call_ != nullptr);
    auto serverlist_wrapper = MakeRefCounted<Serverlist>(serverlist);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
      UniquePtr<char> serverlist_text = serverlist_wrapper->AsText();
      gpr_log(GPR_INFO,
              "[grpclb %p] lb_calld=%p: Serverlist with %" PRIuPTR
              " servers received:\n%s",
              grpclb_policy, lb_calld, serverlist->num_servers,
              serverlist_text.get());
    }
    lb_calld->seen_serverlist_ = true;
    // Start sending client load report only after we start using the
    // serverlist returned from the current LB call.
    if (lb_calld->client_stats_report_interval_ > 0 &&
        lb_calld->client_stats_ == nullptr) {
      lb_calld->client_stats_ = MakeRefCounted<GrpcLbClientStats>();
      // Ref held by callback.
      lb_calld->Ref(DEBUG_LOCATION, "client_load_report").release();
      lb_calld->ScheduleNextClientLoadReportLocked();
    }
    // Check if the serverlist differs from the previous one.
    if (grpclb_policy->serverlist_ != nullptr &&
        *grpclb_policy->serverlist_ == *serverlist_wrapper) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
        gpr_log(GPR_INFO,
                "[grpclb %p] lb_calld=%p: Incoming server list identical to "
                "current, ignoring.",
                grpclb_policy, lb_calld);
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
      if (grpclb_policy->fallback_mode_) {
        gpr_log(GPR_INFO,
                "[grpclb %p] Received response from balancer; exiting "
                "fallback mode",
                grpclb_policy);
        grpclb_policy->fallback_mode_ = false;
      }
      if (grpclb_policy->fallback_at_startup_checks_pending_) {
        grpclb_policy->fallback_at_startup_checks_pending_ = false;
        grpc_timer_cancel(&grpclb_policy->lb_fallback_timer_);
        grpclb_policy->CancelBalancerChannelConnectivityWatchLocked();
      }
      // Update the serverlist in the GrpcLb instance. This serverlist
      // instance will be destroyed either upon the next update or when the
      // GrpcLb instance is destroyed.
      grpclb_policy->serverlist_ = std::move(serverlist_wrapper);
      grpclb_policy->CreateOrUpdateChildPolicyLocked();
    }
  } else {
    // No valid initial response or serverlist found.
    char* response_slice_str =
        grpc_dump_slice(response_slice, GPR_DUMP_ASCII | GPR_DUMP_HEX);
    gpr_log(GPR_ERROR,
            "[grpclb %p] lb_calld=%p: Invalid LB response received: '%s'. "
            "Ignoring.",
            grpclb_policy, lb_calld, response_slice_str);
    gpr_free(response_slice_str);
  }
  grpc_slice_unref_internal(response_slice);
  if (!grpclb_policy->shutting_down_) {
    // Keep listening for serverlist updates.
    grpc_op op;
    memset(&op, 0, sizeof(op));
    op.op = GRPC_OP_RECV_MESSAGE;
    op.data.recv_message.recv_message = &lb_calld->recv_message_payload_;
    op.flags = 0;
    op.reserved = nullptr;
    // Reuse the "OnBalancerMessageReceivedLocked" ref taken in StartQuery().
    const grpc_call_error call_error = grpc_call_start_batch_and_execute(
        lb_calld->lb_call_, &op, 1,
        &lb_calld->lb_on_balancer_message_received_);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  } else {
    lb_calld->Unref(DEBUG_LOCATION, "on_message_received+grpclb_shutdown");
  }
}

void GrpcLb::BalancerCallState::OnBalancerStatusReceivedLocked(
    void* arg, grpc_error* error) {
  BalancerCallState* lb_calld = static_cast<BalancerCallState*>(arg);
  GrpcLb* grpclb_policy = lb_calld->grpclb_policy();
  GPR_ASSERT(lb_calld->lb_call_ != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    char* status_details =
        grpc_slice_to_c_string(lb_calld->lb_call_status_details_);
    gpr_log(GPR_INFO,
            "[grpclb %p] lb_calld=%p: Status from LB server received. "
            "Status = %d, details = '%s', (lb_call: %p), error '%s'",
            grpclb_policy, lb_calld, lb_calld->lb_call_status_, status_details,
            lb_calld->lb_call_, grpc_error_string(error));
    gpr_free(status_details);
  }
  // If this lb_calld is still in use, this call ended because of a failure so
  // we want to retry connecting. Otherwise, we have deliberately ended this
  // call and no further action is required.
  if (lb_calld == grpclb_policy->lb_calld_.get()) {
    // If the fallback-at-startup checks are pending, go into fallback mode
    // immediately.  This short-circuits the timeout for the fallback-at-startup
    // case.
    if (grpclb_policy->fallback_at_startup_checks_pending_) {
      GPR_ASSERT(!lb_calld->seen_serverlist_);
      gpr_log(GPR_INFO,
              "[grpclb %p] Balancer call finished without receiving "
              "serverlist; entering fallback mode",
              grpclb_policy);
      grpclb_policy->fallback_at_startup_checks_pending_ = false;
      grpc_timer_cancel(&grpclb_policy->lb_fallback_timer_);
      grpclb_policy->CancelBalancerChannelConnectivityWatchLocked();
      grpclb_policy->fallback_mode_ = true;
      grpclb_policy->CreateOrUpdateChildPolicyLocked();
    } else {
      // This handles the fallback-after-startup case.
      grpclb_policy->MaybeEnterFallbackModeAfterStartup();
    }
    grpclb_policy->lb_calld_.reset();
    GPR_ASSERT(!grpclb_policy->shutting_down_);
    grpclb_policy->channel_control_helper()->RequestReresolution();
    if (lb_calld->seen_initial_response_) {
      // If we lose connection to the LB server, reset the backoff and restart
      // the LB call immediately.
      grpclb_policy->lb_call_backoff_.Reset();
      grpclb_policy->StartBalancerCallLocked();
    } else {
      // If this LB call fails establishing any connection to the LB server,
      // retry later.
      grpclb_policy->StartBalancerCallRetryTimerLocked();
    }
  }
  lb_calld->Unref(DEBUG_LOCATION, "lb_call_ended");
}

//
// helper code for creating balancer channel
//

ServerAddressList ExtractBalancerAddresses(const ServerAddressList& addresses) {
  ServerAddressList balancer_addresses;
  for (size_t i = 0; i < addresses.size(); ++i) {
    if (addresses[i].IsBalancer()) {
      // Strip out the is_balancer channel arg, since we don't want to
      // recursively use the grpclb policy in the channel used to talk to
      // the balancers.  Note that we do NOT strip out the balancer_name
      // channel arg, since we need that to set the authority correctly
      // to talk to the balancers.
      static const char* args_to_remove[] = {
          GRPC_ARG_ADDRESS_IS_BALANCER,
      };
      balancer_addresses.emplace_back(
          addresses[i].address(),
          grpc_channel_args_copy_and_remove(addresses[i].args(), args_to_remove,
                                            GPR_ARRAY_SIZE(args_to_remove)));
    }
  }
  return balancer_addresses;
}

/* Returns the channel args for the LB channel, used to create a bidirectional
 * stream for the reception of load balancing updates.
 *
 * Inputs:
 *   - \a addresses: corresponding to the balancers.
 *   - \a response_generator: in order to propagate updates from the resolver
 *   above the grpclb policy.
 *   - \a args: other args inherited from the grpclb policy. */
grpc_channel_args* BuildBalancerChannelArgs(
    const ServerAddressList& addresses,
    FakeResolverResponseGenerator* response_generator,
    const grpc_channel_args* args) {
  // Channel args to remove.
  static const char* args_to_remove[] = {
      // LB policy name, since we want to use the default (pick_first) in
      // the LB channel.
      GRPC_ARG_LB_POLICY_NAME,
      // Strip out the service config, since we don't want the LB policy
      // config specified for the parent channel to affect the LB channel.
      GRPC_ARG_SERVICE_CONFIG,
      // The channel arg for the server URI, since that will be different for
      // the LB channel than for the parent channel.  The client channel
      // factory will re-add this arg with the right value.
      GRPC_ARG_SERVER_URI,
      // The fake resolver response generator, because we are replacing it
      // with the one from the grpclb policy, used to propagate updates to
      // the LB channel.
      GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR,
      // The LB channel should use the authority indicated by the target
      // authority table (see \a grpc_lb_policy_grpclb_modify_lb_channel_args),
      // as opposed to the authority from the parent channel.
      GRPC_ARG_DEFAULT_AUTHORITY,
      // Just as for \a GRPC_ARG_DEFAULT_AUTHORITY, the LB channel should be
      // treated as a stand-alone channel and not inherit this argument from the
      // args of the parent channel.
      GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
      // Don't want to pass down channelz node from parent; the balancer
      // channel will get its own.
      GRPC_ARG_CHANNELZ_CHANNEL_NODE,
  };
  // Channel args to add.
  InlinedVector<grpc_arg, 3> args_to_add;
  // The fake resolver response generator, which we use to inject
  // address updates into the LB channel.
  args_to_add.emplace_back(
      grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
          response_generator));
  // A channel arg indicating the target is a grpclb load balancer.
  args_to_add.emplace_back(grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_ADDRESS_IS_GRPCLB_LOAD_BALANCER), 1));
  // The parent channel's channelz uuid.
  channelz::ChannelNode* channelz_node = nullptr;
  const grpc_arg* arg =
      grpc_channel_args_find(args, GRPC_ARG_CHANNELZ_CHANNEL_NODE);
  if (arg != nullptr && arg->type == GRPC_ARG_POINTER &&
      arg->value.pointer.p != nullptr) {
    channelz_node = static_cast<channelz::ChannelNode*>(arg->value.pointer.p);
    args_to_add.emplace_back(
        channelz::MakeParentUuidArg(channelz_node->uuid()));
  }
  // Construct channel args.
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
      args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), args_to_add.data(),
      args_to_add.size());
  // Make any necessary modifications for security.
  return grpc_lb_policy_grpclb_modify_lb_channel_args(addresses, new_args);
}

//
// ctor and dtor
//

GrpcLb::GrpcLb(Args args)
    : LoadBalancingPolicy(std::move(args)),
      response_generator_(MakeRefCounted<FakeResolverResponseGenerator>()),
      lb_call_backoff_(
          BackOff::Options()
              .set_initial_backoff(GRPC_GRPCLB_INITIAL_CONNECT_BACKOFF_SECONDS *
                                   1000)
              .set_multiplier(GRPC_GRPCLB_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_GRPCLB_RECONNECT_JITTER)
              .set_max_backoff(GRPC_GRPCLB_RECONNECT_MAX_BACKOFF_SECONDS *
                               1000)) {
  // Initialization.
  GRPC_CLOSURE_INIT(&lb_on_fallback_, &GrpcLb::OnFallbackTimerLocked, this,
                    grpc_combiner_scheduler(combiner()));
  GRPC_CLOSURE_INIT(&lb_channel_on_connectivity_changed_,
                    &GrpcLb::OnBalancerChannelConnectivityChangedLocked, this,
                    grpc_combiner_scheduler(args.combiner));
  // Record server name.
  const grpc_arg* arg = grpc_channel_args_find(args.args, GRPC_ARG_SERVER_URI);
  const char* server_uri = grpc_channel_arg_get_string(arg);
  GPR_ASSERT(server_uri != nullptr);
  grpc_uri* uri = grpc_uri_parse(server_uri, true);
  GPR_ASSERT(uri->path[0] != '\0');
  server_name_ = gpr_strdup(uri->path[0] == '/' ? uri->path + 1 : uri->path);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Will use '%s' as the server name for LB request.",
            this, server_name_);
  }
  grpc_uri_destroy(uri);
  // Record LB call timeout.
  arg = grpc_channel_args_find(args.args, GRPC_ARG_GRPCLB_CALL_TIMEOUT_MS);
  lb_call_timeout_ms_ = grpc_channel_arg_get_integer(arg, {0, 0, INT_MAX});
  // Record fallback-at-startup timeout.
  arg = grpc_channel_args_find(args.args, GRPC_ARG_GRPCLB_FALLBACK_TIMEOUT_MS);
  fallback_at_startup_timeout_ = grpc_channel_arg_get_integer(
      arg, {GRPC_GRPCLB_DEFAULT_FALLBACK_TIMEOUT_MS, 0, INT_MAX});
}

GrpcLb::~GrpcLb() {
  gpr_free((void*)server_name_);
  grpc_channel_args_destroy(args_);
}

void GrpcLb::ShutdownLocked() {
  shutting_down_ = true;
  lb_calld_.reset();
  if (retry_timer_callback_pending_) {
    grpc_timer_cancel(&lb_call_retry_timer_);
  }
  if (fallback_at_startup_checks_pending_) {
    grpc_timer_cancel(&lb_fallback_timer_);
    CancelBalancerChannelConnectivityWatchLocked();
  }
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
  }
  if (pending_child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(
        pending_child_policy_->interested_parties(), interested_parties());
  }
  child_policy_.reset();
  pending_child_policy_.reset();
  // We destroy the LB channel here instead of in our destructor because
  // destroying the channel triggers a last callback to
  // OnBalancerChannelConnectivityChangedLocked(), and we need to be
  // alive when that callback is invoked.
  if (lb_channel_ != nullptr) {
    grpc_channel_destroy(lb_channel_);
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
  if (pending_child_policy_ != nullptr) {
    pending_child_policy_->ResetBackoffLocked();
  }
}

void GrpcLb::UpdateLocked(UpdateArgs args) {
  const bool is_initial_update = lb_channel_ == nullptr;
  auto* grpclb_config =
      static_cast<const ParsedGrpcLbConfig*>(args.config.get());
  if (grpclb_config != nullptr) {
    child_policy_config_ = grpclb_config->child_policy();
  } else {
    child_policy_config_ = nullptr;
  }
  ProcessAddressesAndChannelArgsLocked(args.addresses, *args.args);
  // Update the existing child policy.
  if (child_policy_ != nullptr) CreateOrUpdateChildPolicyLocked();
  // If this is the initial update, start the fallback-at-startup checks
  // and the balancer call.
  if (is_initial_update) {
    fallback_at_startup_checks_pending_ = true;
    // Start timer.
    grpc_millis deadline = ExecCtx::Get()->Now() + fallback_at_startup_timeout_;
    Ref(DEBUG_LOCATION, "on_fallback_timer").release();  // Ref for callback
    grpc_timer_init(&lb_fallback_timer_, deadline, &lb_on_fallback_);
    // Start watching the channel's connectivity state.  If the channel
    // goes into state TRANSIENT_FAILURE before the timer fires, we go into
    // fallback mode even if the fallback timeout has not elapsed.
    grpc_channel_element* client_channel_elem = grpc_channel_stack_last_element(
        grpc_channel_get_channel_stack(lb_channel_));
    GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
    // Ref held by callback.
    Ref(DEBUG_LOCATION, "watch_lb_channel_connectivity").release();
    grpc_client_channel_watch_connectivity_state(
        client_channel_elem,
        grpc_polling_entity_create_from_pollset_set(interested_parties()),
        &lb_channel_connectivity_, &lb_channel_on_connectivity_changed_,
        nullptr);
    // Start balancer call.
    StartBalancerCallLocked();
  }
}

//
// helpers for UpdateLocked()
//

// Returns the backend addresses extracted from the given addresses.
ServerAddressList ExtractBackendAddresses(const ServerAddressList& addresses) {
  void* lb_token = (void*)GRPC_MDELEM_LB_TOKEN_EMPTY.payload;
  grpc_arg arg = grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_GRPCLB_ADDRESS_LB_TOKEN), lb_token,
      &lb_token_arg_vtable);
  ServerAddressList backend_addresses;
  for (size_t i = 0; i < addresses.size(); ++i) {
    if (!addresses[i].IsBalancer()) {
      backend_addresses.emplace_back(
          addresses[i].address(),
          grpc_channel_args_copy_and_add(addresses[i].args(), &arg, 1));
    }
  }
  return backend_addresses;
}

void GrpcLb::ProcessAddressesAndChannelArgsLocked(
    const ServerAddressList& addresses, const grpc_channel_args& args) {
  // Update fallback address list.
  fallback_backend_addresses_ = ExtractBackendAddresses(addresses);
  // Make sure that GRPC_ARG_LB_POLICY_NAME is set in channel args,
  // since we use this to trigger the client_load_reporting filter.
  static const char* args_to_remove[] = {GRPC_ARG_LB_POLICY_NAME};
  grpc_arg new_arg = grpc_channel_arg_string_create(
      (char*)GRPC_ARG_LB_POLICY_NAME, (char*)"grpclb");
  grpc_channel_args_destroy(args_);
  args_ = grpc_channel_args_copy_and_add_and_remove(
      &args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), &new_arg, 1);
  // Construct args for balancer channel.
  ServerAddressList balancer_addresses = ExtractBalancerAddresses(addresses);
  grpc_channel_args* lb_channel_args = BuildBalancerChannelArgs(
      balancer_addresses, response_generator_.get(), &args);
  // Create balancer channel if needed.
  if (lb_channel_ == nullptr) {
    char* uri_str;
    gpr_asprintf(&uri_str, "fake:///%s", server_name_);
    lb_channel_ =
        channel_control_helper()->CreateChannel(uri_str, *lb_channel_args);
    GPR_ASSERT(lb_channel_ != nullptr);
    gpr_free(uri_str);
  }
  // Propagate updates to the LB channel (pick_first) through the fake
  // resolver.
  Resolver::Result result;
  result.addresses = std::move(balancer_addresses);
  result.args = lb_channel_args;
  response_generator_->SetResponse(std::move(result));
}

void GrpcLb::OnBalancerChannelConnectivityChangedLocked(void* arg,
                                                        grpc_error* error) {
  GrpcLb* self = static_cast<GrpcLb*>(arg);
  if (!self->shutting_down_ && self->fallback_at_startup_checks_pending_) {
    if (self->lb_channel_connectivity_ != GRPC_CHANNEL_TRANSIENT_FAILURE) {
      // Not in TRANSIENT_FAILURE.  Renew connectivity watch.
      grpc_channel_element* client_channel_elem =
          grpc_channel_stack_last_element(
              grpc_channel_get_channel_stack(self->lb_channel_));
      GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
      grpc_client_channel_watch_connectivity_state(
          client_channel_elem,
          grpc_polling_entity_create_from_pollset_set(
              self->interested_parties()),
          &self->lb_channel_connectivity_,
          &self->lb_channel_on_connectivity_changed_, nullptr);
      return;  // Early out so we don't drop the ref below.
    }
    // In TRANSIENT_FAILURE.  Cancel the fallback timer and go into
    // fallback mode immediately.
    gpr_log(GPR_INFO,
            "[grpclb %p] balancer channel in state TRANSIENT_FAILURE; "
            "entering fallback mode",
            self);
    self->fallback_at_startup_checks_pending_ = false;
    grpc_timer_cancel(&self->lb_fallback_timer_);
    self->fallback_mode_ = true;
    self->CreateOrUpdateChildPolicyLocked();
  }
  // Done watching connectivity state, so drop ref.
  self->Unref(DEBUG_LOCATION, "watch_lb_channel_connectivity");
}

void GrpcLb::CancelBalancerChannelConnectivityWatchLocked() {
  grpc_channel_element* client_channel_elem = grpc_channel_stack_last_element(
      grpc_channel_get_channel_stack(lb_channel_));
  GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
  grpc_client_channel_watch_connectivity_state(
      client_channel_elem,
      grpc_polling_entity_create_from_pollset_set(interested_parties()),
      nullptr, &lb_channel_on_connectivity_changed_, nullptr);
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
  grpc_millis next_try = lb_call_backoff_.NextAttemptTime();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO, "[grpclb %p] Connection to LB server lost...", this);
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    if (timeout > 0) {
      gpr_log(GPR_INFO, "[grpclb %p] ... retry_timer_active in %" PRId64 "ms.",
              this, timeout);
    } else {
      gpr_log(GPR_INFO, "[grpclb %p] ... retry_timer_active immediately.",
              this);
    }
  }
  // TODO(roth): We currently track this ref manually.  Once the
  // ClosureRef API is ready, we should pass the RefCountedPtr<> along
  // with the callback.
  auto self = Ref(DEBUG_LOCATION, "on_balancer_call_retry_timer");
  self.release();
  GRPC_CLOSURE_INIT(&lb_on_call_retry_, &GrpcLb::OnBalancerCallRetryTimerLocked,
                    this, grpc_combiner_scheduler(combiner()));
  retry_timer_callback_pending_ = true;
  grpc_timer_init(&lb_call_retry_timer_, next_try, &lb_on_call_retry_);
}

void GrpcLb::OnBalancerCallRetryTimerLocked(void* arg, grpc_error* error) {
  GrpcLb* grpclb_policy = static_cast<GrpcLb*>(arg);
  grpclb_policy->retry_timer_callback_pending_ = false;
  if (!grpclb_policy->shutting_down_ && error == GRPC_ERROR_NONE &&
      grpclb_policy->lb_calld_ == nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
      gpr_log(GPR_INFO, "[grpclb %p] Restarting call to LB server",
              grpclb_policy);
    }
    grpclb_policy->StartBalancerCallLocked();
  }
  grpclb_policy->Unref(DEBUG_LOCATION, "on_balancer_call_retry_timer");
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

void GrpcLb::OnFallbackTimerLocked(void* arg, grpc_error* error) {
  GrpcLb* grpclb_policy = static_cast<GrpcLb*>(arg);
  // If we receive a serverlist after the timer fires but before this callback
  // actually runs, don't fall back.
  if (grpclb_policy->fallback_at_startup_checks_pending_ &&
      !grpclb_policy->shutting_down_ && error == GRPC_ERROR_NONE) {
    gpr_log(GPR_INFO,
            "[grpclb %p] No response from balancer after fallback timeout; "
            "entering fallback mode",
            grpclb_policy);
    grpclb_policy->fallback_at_startup_checks_pending_ = false;
    grpclb_policy->CancelBalancerChannelConnectivityWatchLocked();
    grpclb_policy->fallback_mode_ = true;
    grpclb_policy->CreateOrUpdateChildPolicyLocked();
  }
  grpclb_policy->Unref(DEBUG_LOCATION, "on_fallback_timer");
}

//
// code for interacting with the child policy
//

grpc_channel_args* GrpcLb::CreateChildPolicyArgsLocked(
    bool is_backend_from_grpclb_load_balancer) {
  InlinedVector<grpc_arg, 2> args_to_add;
  args_to_add.emplace_back(grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_ADDRESS_IS_BACKEND_FROM_GRPCLB_LOAD_BALANCER),
      is_backend_from_grpclb_load_balancer));
  if (is_backend_from_grpclb_load_balancer) {
    args_to_add.emplace_back(grpc_channel_arg_integer_create(
        const_cast<char*>(GRPC_ARG_INHIBIT_HEALTH_CHECKING), 1));
  }
  return grpc_channel_args_copy_and_add(args_, args_to_add.data(),
                                        args_to_add.size());
}

OrphanablePtr<LoadBalancingPolicy> GrpcLb::CreateChildPolicyLocked(
    const char* name, const grpc_channel_args* args) {
  Helper* helper = New<Helper>(Ref());
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.combiner = combiner();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      UniquePtr<ChannelControlHelper>(helper);
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
          name, std::move(lb_policy_args));
  if (GPR_UNLIKELY(lb_policy == nullptr)) {
    gpr_log(GPR_ERROR, "[grpclb %p] Failure creating child policy %s", this,
            name);
    return nullptr;
  }
  helper->set_child(lb_policy.get());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO, "[grpclb %p] Created new child policy %s (%p)", this,
            name, lb_policy.get());
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
    // received any serverlist from the balancer, we use the fallback backends
    // returned by the resolver. Note that the fallback backend list may be
    // empty, in which case the new round_robin policy will keep the requested
    // picks pending.
    update_args.addresses = fallback_backend_addresses_;
  } else {
    update_args.addresses = serverlist_->GetServerAddressList(
        lb_calld_ == nullptr ? nullptr : lb_calld_->client_stats());
    is_backend_from_grpclb_load_balancer = true;
  }
  update_args.args =
      CreateChildPolicyArgsLocked(is_backend_from_grpclb_load_balancer);
  GPR_ASSERT(update_args.args != nullptr);
  update_args.config = child_policy_config_;
  // If the child policy name changes, we need to create a new child
  // policy.  When this happens, we leave child_policy_ as-is and store
  // the new child policy in pending_child_policy_.  Once the new child
  // policy transitions into state READY, we swap it into child_policy_,
  // replacing the original child policy.  So pending_child_policy_ is
  // non-null only between when we apply an update that changes the child
  // policy name and when the new child reports state READY.
  //
  // Updates can arrive at any point during this transition.  We always
  // apply updates relative to the most recently created child policy,
  // even if the most recent one is still in pending_child_policy_.  This
  // is true both when applying the updates to an existing child policy
  // and when determining whether we need to create a new policy.
  //
  // As a result of this, there are several cases to consider here:
  //
  // 1. We have no existing child policy (i.e., we have started up but
  //    have not yet received a serverlist from the balancer or gone
  //    into fallback mode; in this case, both child_policy_ and
  //    pending_child_policy_ are null).  In this case, we create a
  //    new child policy and store it in child_policy_.
  //
  // 2. We have an existing child policy and have no pending child policy
  //    from a previous update (i.e., either there has not been a
  //    previous update that changed the policy name, or we have already
  //    finished swapping in the new policy; in this case, child_policy_
  //    is non-null but pending_child_policy_ is null).  In this case:
  //    a. If child_policy_->name() equals child_policy_name, then we
  //       update the existing child policy.
  //    b. If child_policy_->name() does not equal child_policy_name,
  //       we create a new policy.  The policy will be stored in
  //       pending_child_policy_ and will later be swapped into
  //       child_policy_ by the helper when the new child transitions
  //       into state READY.
  //
  // 3. We have an existing child policy and have a pending child policy
  //    from a previous update (i.e., a previous update set
  //    pending_child_policy_ as per case 2b above and that policy has
  //    not yet transitioned into state READY and been swapped into
  //    child_policy_; in this case, both child_policy_ and
  //    pending_child_policy_ are non-null).  In this case:
  //    a. If pending_child_policy_->name() equals child_policy_name,
  //       then we update the existing pending child policy.
  //    b. If pending_child_policy->name() does not equal
  //       child_policy_name, then we create a new policy.  The new
  //       policy is stored in pending_child_policy_ (replacing the one
  //       that was there before, which will be immediately shut down)
  //       and will later be swapped into child_policy_ by the helper
  //       when the new child transitions into state READY.
  const char* child_policy_name = child_policy_config_ == nullptr
                                      ? "round_robin"
                                      : child_policy_config_->name();
  const bool create_policy =
      // case 1
      child_policy_ == nullptr ||
      // case 2b
      (pending_child_policy_ == nullptr &&
       strcmp(child_policy_->name(), child_policy_name) != 0) ||
      // case 3b
      (pending_child_policy_ != nullptr &&
       strcmp(pending_child_policy_->name(), child_policy_name) != 0);
  LoadBalancingPolicy* policy_to_update = nullptr;
  if (create_policy) {
    // Cases 1, 2b, and 3b: create a new child policy.
    // If child_policy_ is null, we set it (case 1), else we set
    // pending_child_policy_ (cases 2b and 3b).
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
      gpr_log(GPR_INFO, "[grpclb %p] Creating new %schild policy %s", this,
              child_policy_ == nullptr ? "" : "pending ", child_policy_name);
    }
    // Swap the policy into place.
    auto& lb_policy =
        child_policy_ == nullptr ? child_policy_ : pending_child_policy_;
    lb_policy = CreateChildPolicyLocked(child_policy_name, update_args.args);
    policy_to_update = lb_policy.get();
  } else {
    // Cases 2a and 3a: update an existing policy.
    // If we have a pending child policy, send the update to the pending
    // policy (case 3a), else send it to the current policy (case 2a).
    policy_to_update = pending_child_policy_ != nullptr
                           ? pending_child_policy_.get()
                           : child_policy_.get();
  }
  GPR_ASSERT(policy_to_update != nullptr);
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_glb_trace)) {
    gpr_log(GPR_INFO, "[grpclb %p] Updating %schild policy %p", this,
            policy_to_update == pending_child_policy_.get() ? "pending " : "",
            policy_to_update);
  }
  policy_to_update->UpdateLocked(std::move(update_args));
}

//
// factory
//

class GrpcLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return OrphanablePtr<LoadBalancingPolicy>(New<GrpcLb>(std::move(args)));
  }

  const char* name() const override { return kGrpclb; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const grpc_json* json, grpc_error** error) const override {
    GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
    if (json == nullptr) {
      return RefCountedPtr<LoadBalancingPolicy::Config>(
          New<ParsedGrpcLbConfig>(nullptr));
    }
    InlinedVector<grpc_error*, 2> error_list;
    RefCountedPtr<LoadBalancingPolicy::Config> child_policy;
    for (const grpc_json* field = json->child; field != nullptr;
         field = field->next) {
      if (field->key == nullptr) continue;
      if (strcmp(field->key, "childPolicy") == 0) {
        if (child_policy != nullptr) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:childPolicy error:Duplicate entry"));
        }
        grpc_error* parse_error = GRPC_ERROR_NONE;
        child_policy = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
            field, &parse_error);
        if (parse_error != GRPC_ERROR_NONE) {
          error_list.push_back(parse_error);
        }
      }
    }
    if (error_list.empty()) {
      return RefCountedPtr<LoadBalancingPolicy::Config>(
          New<ParsedGrpcLbConfig>(std::move(child_policy)));
    } else {
      *error = GRPC_ERROR_CREATE_FROM_VECTOR("GrpcLb Parser", &error_list);
      return nullptr;
    }
  }
};

}  // namespace

}  // namespace grpc_core

//
// Plugin registration
//

namespace {

// Only add client_load_reporting filter if the grpclb LB policy is used.
bool maybe_add_client_load_reporting_filter(grpc_channel_stack_builder* builder,
                                            void* arg) {
  const grpc_channel_args* args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  const grpc_arg* channel_arg =
      grpc_channel_args_find(args, GRPC_ARG_LB_POLICY_NAME);
  if (channel_arg != nullptr && channel_arg->type == GRPC_ARG_STRING &&
      strcmp(channel_arg->value.string, "grpclb") == 0) {
    return grpc_channel_stack_builder_append_filter(
        builder, (const grpc_channel_filter*)arg, nullptr, nullptr);
  }
  return true;
}

}  // namespace

void grpc_lb_policy_grpclb_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          grpc_core::UniquePtr<grpc_core::LoadBalancingPolicyFactory>(
              grpc_core::New<grpc_core::GrpcLbFactory>()));
  grpc_channel_init_register_stage(GRPC_CLIENT_SUBCHANNEL,
                                   GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
                                   maybe_add_client_load_reporting_filter,
                                   (void*)&grpc_client_load_reporting_filter);
}

void grpc_lb_policy_grpclb_shutdown() {}
