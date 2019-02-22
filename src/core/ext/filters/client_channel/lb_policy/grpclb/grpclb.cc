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
#include "src/core/lib/gprpp/mutex_lock.h"
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

class GrpcLb : public LoadBalancingPolicy {
 public:
  explicit GrpcLb(Args args);

  const char* name() const override { return kGrpclb; }

  void UpdateLocked(const grpc_channel_args& args,
                    RefCountedPtr<Config> lb_config) override;
  void ResetBackoffLocked() override;
  void FillChildRefsForChannelz(
      channelz::ChildRefsList* child_subchannels,
      channelz::ChildRefsList* child_channels) override;

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
    ServerAddressList GetServerAddressList() const;

    // Returns true if the serverlist contains at least one drop entry and
    // no backend address entries.
    bool ContainsAllDropEntries() const;

    // Returns the LB token to use for a drop, or null if the call
    // should not be dropped.
    // Intended to be called from picker, so calls will be externally
    // synchronized.
    const char* ShouldDrop();

   private:
    grpc_grpclb_serverlist* serverlist_;
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

    PickResult Pick(PickState* pick, grpc_error** error) override;

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

    Subchannel* CreateSubchannel(const grpc_channel_args& args) override;
    grpc_channel* CreateChannel(const char* target,
                                grpc_client_channel_type type,
                                const grpc_channel_args& args) override;
    void UpdateState(grpc_connectivity_state state, grpc_error* state_error,
                     UniquePtr<SubchannelPicker> picker) override;
    void RequestReresolution() override;

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
  void ProcessChannelArgsLocked(const grpc_channel_args& args);
  void ParseLbConfig(Config* grpclb_config);

  // Methods for dealing with the balancer channel and call.
  void StartBalancerCallLocked();
  static void OnFallbackTimerLocked(void* arg, grpc_error* error);
  void StartBalancerCallRetryTimerLocked();
  static void OnBalancerCallRetryTimerLocked(void* arg, grpc_error* error);
  static void OnBalancerChannelConnectivityChangedLocked(void* arg,
                                                         grpc_error* error);

  // Methods for dealing with the child policy.
  grpc_channel_args* CreateChildPolicyArgsLocked();
  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const char* name, grpc_channel_args* args);
  void CreateOrUpdateChildPolicyLocked();

  // Who the client is trying to communicate with.
  const char* server_name_ = nullptr;

  // Current channel args from the resolver.
  grpc_channel_args* args_ = nullptr;

  // Internal state.
  bool shutting_down_ = false;

  // The channel for communicating with the LB server.
  grpc_channel* lb_channel_ = nullptr;
  // Uuid of the lb channel. Used for channelz.
  gpr_atm lb_channel_uuid_ = 0;
  grpc_connectivity_state lb_channel_connectivity_;
  grpc_closure lb_channel_on_connectivity_changed_;
  // Are we already watching the LB channel's connectivity?
  bool watching_lb_channel_ = false;
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

  // Timeout in milliseconds for before using fallback backend addresses.
  // 0 means not using fallback.
  int lb_fallback_timeout_ms_ = 0;
  // The backend addresses from the resolver.
  UniquePtr<ServerAddressList> fallback_backend_addresses_;
  // Fallback timer.
  bool fallback_timer_callback_pending_ = false;
  grpc_timer lb_fallback_timer_;
  grpc_closure lb_on_fallback_;

  // The child policy to use for the backends.
  OrphanablePtr<LoadBalancingPolicy> child_policy_;
  // When switching child policies, the new policy will be stored here
  // until it reports READY, at which point it will be moved to child_policy_.
  OrphanablePtr<LoadBalancingPolicy> pending_child_policy_;
  // The LB config to use for the child policy.
  UniquePtr<char> child_policy_name_;
  RefCountedPtr<Config> child_policy_config_;
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
ServerAddressList GrpcLb::Serverlist::GetServerAddressList() const {
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

// Adds lb_token of selected subchannel (address) to the call's initial
// metadata.
grpc_error* AddLbTokenToInitialMetadata(
    grpc_mdelem lb_token, grpc_linked_mdelem* lb_token_mdelem_storage,
    grpc_metadata_batch* initial_metadata) {
  GPR_ASSERT(lb_token_mdelem_storage != nullptr);
  GPR_ASSERT(!GRPC_MDISNULL(lb_token));
  return grpc_metadata_batch_add_tail(initial_metadata, lb_token_mdelem_storage,
                                      lb_token);
}

// Destroy function used when embedding client stats in call context.
void DestroyClientStats(void* arg) {
  static_cast<GrpcLbClientStats*>(arg)->Unref();
}

GrpcLb::Picker::PickResult GrpcLb::Picker::Pick(PickState* pick,
                                                grpc_error** error) {
  // Check if we should drop the call.
  const char* drop_token = serverlist_->ShouldDrop();
  if (drop_token != nullptr) {
    // Update client load reporting stats to indicate the number of
    // dropped calls.  Note that we have to do this here instead of in
    // the client_load_reporting filter, because we do not create a
    // subchannel call (and therefore no client_load_reporting filter)
    // for dropped calls.
    if (client_stats_ != nullptr) {
      client_stats_->AddCallDroppedLocked(drop_token);
    }
    return PICK_COMPLETE;
  }
  // Forward pick to child policy.
  PickResult result = child_picker_->Pick(pick, error);
  // If pick succeeded, add LB token to initial metadata.
  if (result == PickResult::PICK_COMPLETE &&
      pick->connected_subchannel != nullptr) {
    const grpc_arg* arg = grpc_channel_args_find(
        pick->connected_subchannel->args(), GRPC_ARG_GRPCLB_ADDRESS_LB_TOKEN);
    if (arg == nullptr) {
      gpr_log(GPR_ERROR,
              "[grpclb %p picker %p] No LB token for connected subchannel "
              "pick %p",
              parent_, this, pick);
      abort();
    }
    grpc_mdelem lb_token = {reinterpret_cast<uintptr_t>(arg->value.pointer.p)};
    AddLbTokenToInitialMetadata(GRPC_MDELEM_REF(lb_token),
                                &pick->lb_token_mdelem_storage,
                                pick->initial_metadata);
    // Pass on client stats via context. Passes ownership of the reference.
    if (client_stats_ != nullptr) {
      pick->subchannel_call_context[GRPC_GRPCLB_CLIENT_STATS].value =
          client_stats_->Ref().release();
      pick->subchannel_call_context[GRPC_GRPCLB_CLIENT_STATS].destroy =
          DestroyClientStats;
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

Subchannel* GrpcLb::Helper::CreateSubchannel(const grpc_channel_args& args) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return parent_->channel_control_helper()->CreateSubchannel(args);
}

grpc_channel* GrpcLb::Helper::CreateChannel(const char* target,
                                            grpc_client_channel_type type,
                                            const grpc_channel_args& args) {
  if (parent_->shutting_down_ ||
      (!CalledByPendingChild() && !CalledByCurrentChild())) {
    return nullptr;
  }
  return parent_->channel_control_helper()->CreateChannel(target, type, args);
}

void GrpcLb::Helper::UpdateState(grpc_connectivity_state state,
                                 grpc_error* state_error,
                                 UniquePtr<SubchannelPicker> picker) {
  if (parent_->shutting_down_) {
    GRPC_ERROR_UNREF(state_error);
    return;
  }
  // If this request is from the pending child policy, ignore it until
  // it reports READY, at which point we swap it into place.
  if (CalledByPendingChild()) {
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[grpclb %p helper %p] pending child policy %p reports state=%s",
              parent_.get(), this, parent_->pending_child_policy_.get(),
              grpc_connectivity_state_name(state));
    }
    if (state != GRPC_CHANNEL_READY) return;
    parent_->child_policy_ = std::move(parent_->pending_child_policy_);
  } else if (!CalledByCurrentChild()) {
    // This request is from an outdating pending child, so ignore it.
    return;
  }
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
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[grpclb %p helper %p] state=%s passing child picker %p as-is",
              parent_.get(), this, grpc_connectivity_state_name(state),
              picker.get());
    }
    parent_->channel_control_helper()->UpdateState(state, state_error,
                                                   std::move(picker));
    return;
  }
  // Cases 2 and 3a: wrap picker from the child in our own picker.
  if (grpc_lb_glb_trace.enabled()) {
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
      state, state_error,
      UniquePtr<SubchannelPicker>(
          New<Picker>(parent_.get(), parent_->serverlist_, std::move(picker),
                      std::move(client_stats))));
}

void GrpcLb::Helper::RequestReresolution() {
  if (parent_->shutting_down_) return;
  // If there is a pending child policy, ignore re-resolution requests
  // from the current child policy.
  if (parent_->pending_child_policy_ != nullptr && !CalledByPendingChild()) {
    return;
  }
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Re-resolution requested from child policy (%p).",
            parent_.get(), child_);
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
  if (grpc_lb_glb_trace.enabled()) {
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
  op->flags = 0;
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
};

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
      grpc_grpclb_load_report_request_create_locked(client_stats_.get());
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
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[grpclb %p] lb_calld=%p: Received initial LB response "
                "message; client load reporting interval = %" PRId64
                " milliseconds",
                grpclb_policy, lb_calld,
                lb_calld->client_stats_report_interval_);
      }
    } else if (grpc_lb_glb_trace.enabled()) {
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
    if (grpc_lb_glb_trace.enabled()) {
      UniquePtr<char> serverlist_text = serverlist_wrapper->AsText();
      gpr_log(GPR_INFO,
              "[grpclb %p] lb_calld=%p: Serverlist with %" PRIuPTR
              " servers received:\n%s",
              grpclb_policy, lb_calld, serverlist->num_servers,
              serverlist_text.get());
    }
    // Start sending client load report only after we start using the
    // serverlist returned from the current LB call.
    if (lb_calld->client_stats_report_interval_ > 0 &&
        lb_calld->client_stats_ == nullptr) {
      lb_calld->client_stats_ = MakeRefCounted<GrpcLbClientStats>();
      // TODO(roth): We currently track this ref manually.  Once the
      // ClosureRef API is ready, we should pass the RefCountedPtr<> along
      // with the callback.
      auto self = lb_calld->Ref(DEBUG_LOCATION, "client_load_report");
      self.release();
      lb_calld->ScheduleNextClientLoadReportLocked();
    }
    // Check if the serverlist differs from the previous one.
    if (grpclb_policy->serverlist_ != nullptr &&
        *grpclb_policy->serverlist_ == *serverlist_wrapper) {
      if (grpc_lb_glb_trace.enabled()) {
        gpr_log(GPR_INFO,
                "[grpclb %p] lb_calld=%p: Incoming server list identical to "
                "current, ignoring.",
                grpclb_policy, lb_calld);
      }
    } else {  // New serverlist.
      if (grpclb_policy->serverlist_ == nullptr) {
        // Dispose of the fallback.
        grpclb_policy->fallback_backend_addresses_.reset();
        if (grpclb_policy->fallback_timer_callback_pending_) {
          grpc_timer_cancel(&grpclb_policy->lb_fallback_timer_);
        }
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
  if (grpc_lb_glb_trace.enabled()) {
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
  ServerAddressList balancer_addresses = ExtractBalancerAddresses(addresses);
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
      // The resolved addresses, which will be generated by the name resolver
      // used in the LB channel.  Note that the LB channel will use the fake
      // resolver, so this won't actually generate a query to DNS (or some
      // other name service).  However, the addresses returned by the fake
      // resolver will have is_balancer=false, whereas our own addresses have
      // is_balancer=true.  We need the LB channel to return addresses with
      // is_balancer=false so that it does not wind up recursively using the
      // grpclb LB policy.
      GRPC_ARG_SERVER_ADDRESS_LIST,
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
  };
  // Channel args to add.
  const grpc_arg args_to_add[] = {
      // New address list.
      // Note that we pass these in both when creating the LB channel
      // and via the fake resolver.  The latter is what actually gets used.
      CreateServerAddressListChannelArg(&balancer_addresses),
      // The fake resolver response generator, which we use to inject
      // address updates into the LB channel.
      grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
          response_generator),
      // A channel arg indicating the target is a grpclb load balancer.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_ADDRESS_IS_GRPCLB_LOAD_BALANCER), 1),
      // A channel arg indicating this is an internal channels, aka it is
      // owned by components in Core, not by the user application.
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_CHANNELZ_CHANNEL_IS_INTERNAL_CHANNEL), 1),
  };
  // Construct channel args.
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
      args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), args_to_add,
      GPR_ARRAY_SIZE(args_to_add));
  // Make any necessary modifications for security.
  return grpc_lb_policy_grpclb_modify_lb_channel_args(new_args);
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
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Will use '%s' as the server name for LB request.",
            this, server_name_);
  }
  grpc_uri_destroy(uri);
  // Record LB call timeout.
  arg = grpc_channel_args_find(args.args, GRPC_ARG_GRPCLB_CALL_TIMEOUT_MS);
  lb_call_timeout_ms_ = grpc_channel_arg_get_integer(arg, {0, 0, INT_MAX});
  // Record fallback timeout.
  arg = grpc_channel_args_find(args.args, GRPC_ARG_GRPCLB_FALLBACK_TIMEOUT_MS);
  lb_fallback_timeout_ms_ = grpc_channel_arg_get_integer(
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
  if (fallback_timer_callback_pending_) {
    grpc_timer_cancel(&lb_fallback_timer_);
  }
  child_policy_.reset();
  // We destroy the LB channel here instead of in our destructor because
  // destroying the channel triggers a last callback to
  // OnBalancerChannelConnectivityChangedLocked(), and we need to be
  // alive when that callback is invoked.
  if (lb_channel_ != nullptr) {
    grpc_channel_destroy(lb_channel_);
    lb_channel_ = nullptr;
    gpr_atm_no_barrier_store(&lb_channel_uuid_, 0);
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

void GrpcLb::FillChildRefsForChannelz(
    channelz::ChildRefsList* child_subchannels,
    channelz::ChildRefsList* child_channels) {
  // delegate to the child policy to fill the children subchannels.
  if (child_policy_ != nullptr) {
    child_policy_->FillChildRefsForChannelz(child_subchannels, child_channels);
  }
  gpr_atm uuid = gpr_atm_no_barrier_load(&lb_channel_uuid_);
  if (uuid != 0) {
    child_channels->push_back(uuid);
  }
}

void GrpcLb::UpdateLocked(const grpc_channel_args& args,
                          RefCountedPtr<Config> lb_config) {
  const bool is_initial_update = lb_channel_ == nullptr;
  ParseLbConfig(lb_config.get());
  ProcessChannelArgsLocked(args);
  // Update the existing child policy.
  if (child_policy_ != nullptr) CreateOrUpdateChildPolicyLocked();
  // If this is the initial update, start the fallback timer.
  if (is_initial_update) {
    if (lb_fallback_timeout_ms_ > 0 && serverlist_ == nullptr &&
        !fallback_timer_callback_pending_) {
      grpc_millis deadline = ExecCtx::Get()->Now() + lb_fallback_timeout_ms_;
      Ref(DEBUG_LOCATION, "on_fallback_timer").release();  // Ref for callback
      GRPC_CLOSURE_INIT(&lb_on_fallback_, &GrpcLb::OnFallbackTimerLocked, this,
                        grpc_combiner_scheduler(combiner()));
      fallback_timer_callback_pending_ = true;
      grpc_timer_init(&lb_fallback_timer_, deadline, &lb_on_fallback_);
    }
    StartBalancerCallLocked();
  } else if (!watching_lb_channel_) {
    // If this is not the initial update and we're not already watching
    // the LB channel's connectivity state, start a watch now.  This
    // ensures that we'll know when to switch to a new balancer call.
    lb_channel_connectivity_ = grpc_channel_check_connectivity_state(
        lb_channel_, true /* try to connect */);
    grpc_channel_element* client_channel_elem = grpc_channel_stack_last_element(
        grpc_channel_get_channel_stack(lb_channel_));
    GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
    watching_lb_channel_ = true;
    // Ref held by callback.
    Ref(DEBUG_LOCATION, "watch_lb_channel_connectivity").release();
    grpc_client_channel_watch_connectivity_state(
        client_channel_elem,
        grpc_polling_entity_create_from_pollset_set(interested_parties()),
        &lb_channel_connectivity_, &lb_channel_on_connectivity_changed_,
        nullptr);
  }
}

//
// helpers for UpdateLocked()
//

// Returns the backend addresses extracted from the given addresses.
UniquePtr<ServerAddressList> ExtractBackendAddresses(
    const ServerAddressList& addresses) {
  void* lb_token = (void*)GRPC_MDELEM_LB_TOKEN_EMPTY.payload;
  grpc_arg arg = grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_GRPCLB_ADDRESS_LB_TOKEN), lb_token,
      &lb_token_arg_vtable);
  auto backend_addresses = MakeUnique<ServerAddressList>();
  for (size_t i = 0; i < addresses.size(); ++i) {
    if (!addresses[i].IsBalancer()) {
      backend_addresses->emplace_back(
          addresses[i].address(),
          grpc_channel_args_copy_and_add(addresses[i].args(), &arg, 1));
    }
  }
  return backend_addresses;
}

void GrpcLb::ProcessChannelArgsLocked(const grpc_channel_args& args) {
  const ServerAddressList* addresses = FindServerAddressListChannelArg(&args);
  if (addresses == nullptr) {
    // Ignore this update.
    gpr_log(
        GPR_ERROR,
        "[grpclb %p] No valid LB addresses channel arg in update, ignoring.",
        this);
    return;
  }
  // Update fallback address list.
  fallback_backend_addresses_ = ExtractBackendAddresses(*addresses);
  // Make sure that GRPC_ARG_LB_POLICY_NAME is set in channel args,
  // since we use this to trigger the client_load_reporting filter.
  static const char* args_to_remove[] = {GRPC_ARG_LB_POLICY_NAME};
  grpc_arg new_arg = grpc_channel_arg_string_create(
      (char*)GRPC_ARG_LB_POLICY_NAME, (char*)"grpclb");
  grpc_channel_args_destroy(args_);
  args_ = grpc_channel_args_copy_and_add_and_remove(
      &args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), &new_arg, 1);
  // Construct args for balancer channel.
  grpc_channel_args* lb_channel_args =
      BuildBalancerChannelArgs(*addresses, response_generator_.get(), &args);
  // Create balancer channel if needed.
  if (lb_channel_ == nullptr) {
    char* uri_str;
    gpr_asprintf(&uri_str, "fake:///%s", server_name_);
    lb_channel_ = channel_control_helper()->CreateChannel(
        uri_str, GRPC_CLIENT_CHANNEL_TYPE_LOAD_BALANCING, *lb_channel_args);
    GPR_ASSERT(lb_channel_ != nullptr);
    grpc_core::channelz::ChannelNode* channel_node =
        grpc_channel_get_channelz_node(lb_channel_);
    if (channel_node != nullptr) {
      gpr_atm_no_barrier_store(&lb_channel_uuid_, channel_node->uuid());
    }
    gpr_free(uri_str);
  }
  // Propagate updates to the LB channel (pick_first) through the fake
  // resolver.
  response_generator_->SetResponse(lb_channel_args);
  grpc_channel_args_destroy(lb_channel_args);
}

void GrpcLb::ParseLbConfig(Config* grpclb_config) {
  const grpc_json* child_policy = nullptr;
  if (grpclb_config != nullptr) {
    const grpc_json* grpclb_config_json = grpclb_config->json();
    for (const grpc_json* field = grpclb_config_json; field != nullptr;
         field = field->next) {
      if (field->key == nullptr) return;
      if (strcmp(field->key, "childPolicy") == 0) {
        if (child_policy != nullptr) return;  // Duplicate.
        child_policy = ParseLoadBalancingConfig(field);
      }
    }
  }
  if (child_policy != nullptr) {
    child_policy_name_ = UniquePtr<char>(gpr_strdup(child_policy->key));
    child_policy_config_ = MakeRefCounted<Config>(
        child_policy->child, grpclb_config->service_config());
  } else {
    child_policy_name_.reset();
    child_policy_config_.reset();
  }
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
  if (grpc_lb_glb_trace.enabled()) {
    gpr_log(GPR_INFO,
            "[grpclb %p] Query for backends (lb_channel: %p, lb_calld: %p)",
            this, lb_channel_, lb_calld_.get());
  }
  lb_calld_->StartQuery();
}

void GrpcLb::OnFallbackTimerLocked(void* arg, grpc_error* error) {
  GrpcLb* grpclb_policy = static_cast<GrpcLb*>(arg);
  grpclb_policy->fallback_timer_callback_pending_ = false;
  // If we receive a serverlist after the timer fires but before this callback
  // actually runs, don't fall back.
  if (grpclb_policy->serverlist_ == nullptr && !grpclb_policy->shutting_down_ &&
      error == GRPC_ERROR_NONE) {
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO,
              "[grpclb %p] Falling back to use backends from resolver",
              grpclb_policy);
    }
    GPR_ASSERT(grpclb_policy->fallback_backend_addresses_ != nullptr);
    grpclb_policy->CreateOrUpdateChildPolicyLocked();
  }
  grpclb_policy->Unref(DEBUG_LOCATION, "on_fallback_timer");
}

void GrpcLb::StartBalancerCallRetryTimerLocked() {
  grpc_millis next_try = lb_call_backoff_.NextAttemptTime();
  if (grpc_lb_glb_trace.enabled()) {
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
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO, "[grpclb %p] Restarting call to LB server",
              grpclb_policy);
    }
    grpclb_policy->StartBalancerCallLocked();
  }
  grpclb_policy->Unref(DEBUG_LOCATION, "on_balancer_call_retry_timer");
}

// Invoked as part of the update process. It continues watching the LB channel
// until it shuts down or becomes READY. It's invoked even if the LB channel
// stayed READY throughout the update (for example if the update is identical).
void GrpcLb::OnBalancerChannelConnectivityChangedLocked(void* arg,
                                                        grpc_error* error) {
  GrpcLb* grpclb_policy = static_cast<GrpcLb*>(arg);
  if (grpclb_policy->shutting_down_) goto done;
  // Re-initialize the lb_call. This should also take care of updating the
  // child policy. Note that the current child policy, if any, will stay in
  // effect until an update from the new lb_call is received.
  switch (grpclb_policy->lb_channel_connectivity_) {
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      // Keep watching the LB channel.
      grpc_channel_element* client_channel_elem =
          grpc_channel_stack_last_element(
              grpc_channel_get_channel_stack(grpclb_policy->lb_channel_));
      GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
      grpc_client_channel_watch_connectivity_state(
          client_channel_elem,
          grpc_polling_entity_create_from_pollset_set(
              grpclb_policy->interested_parties()),
          &grpclb_policy->lb_channel_connectivity_,
          &grpclb_policy->lb_channel_on_connectivity_changed_, nullptr);
      break;
    }
      // The LB channel may be IDLE because it's shut down before the update.
      // Restart the LB call to kick the LB channel into gear.
    case GRPC_CHANNEL_IDLE:
    case GRPC_CHANNEL_READY:
      grpclb_policy->lb_calld_.reset();
      if (grpclb_policy->retry_timer_callback_pending_) {
        grpc_timer_cancel(&grpclb_policy->lb_call_retry_timer_);
      }
      grpclb_policy->lb_call_backoff_.Reset();
      grpclb_policy->StartBalancerCallLocked();
      // fallthrough
    case GRPC_CHANNEL_SHUTDOWN:
    done:
      grpclb_policy->watching_lb_channel_ = false;
      grpclb_policy->Unref(DEBUG_LOCATION,
                           "watch_lb_channel_connectivity_cb_shutdown");
  }
}

//
// code for interacting with the child policy
//

grpc_channel_args* GrpcLb::CreateChildPolicyArgsLocked() {
  ServerAddressList tmp_addresses;
  ServerAddressList* addresses = &tmp_addresses;
  bool is_backend_from_grpclb_load_balancer = false;
  if (serverlist_ != nullptr) {
    tmp_addresses = serverlist_->GetServerAddressList();
    is_backend_from_grpclb_load_balancer = true;
  } else {
    // If CreateOrUpdateChildPolicyLocked() is invoked when we haven't
    // received any serverlist from the balancer, we use the fallback backends
    // returned by the resolver. Note that the fallback backend list may be
    // empty, in which case the new round_robin policy will keep the requested
    // picks pending.
    GPR_ASSERT(fallback_backend_addresses_ != nullptr);
    addresses = fallback_backend_addresses_.get();
  }
  GPR_ASSERT(addresses != nullptr);
  // Replace the server address list in the channel args that we pass down to
  // the subchannel.
  static const char* keys_to_remove[] = {GRPC_ARG_SERVER_ADDRESS_LIST};
  grpc_arg args_to_add[3] = {
      CreateServerAddressListChannelArg(addresses),
      // A channel arg indicating if the target is a backend inferred from a
      // grpclb load balancer.
      grpc_channel_arg_integer_create(
          const_cast<char*>(
              GRPC_ARG_ADDRESS_IS_BACKEND_FROM_GRPCLB_LOAD_BALANCER),
          is_backend_from_grpclb_load_balancer),
  };
  size_t num_args_to_add = 2;
  if (is_backend_from_grpclb_load_balancer) {
    args_to_add[2] = grpc_channel_arg_integer_create(
        const_cast<char*>(GRPC_ARG_INHIBIT_HEALTH_CHECKING), 1);
    ++num_args_to_add;
  }
  grpc_channel_args* args = grpc_channel_args_copy_and_add_and_remove(
      args_, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), args_to_add,
      num_args_to_add);
  return args;
}

OrphanablePtr<LoadBalancingPolicy> GrpcLb::CreateChildPolicyLocked(
    const char* name, grpc_channel_args* args) {
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
  if (grpc_lb_glb_trace.enabled()) {
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
  grpc_channel_args* args = CreateChildPolicyArgsLocked();
  GPR_ASSERT(args != nullptr);
  // There are several cases here:
  // 1. If we do not yet have any child policy, we create one and store
  //    it in child_policy_.
  // 2. If we have a child policy but no pending child policy, then:
  //    a. If the child policy's name equals child_policy_name, then we
  //       update the existing child policy.
  //    b. If the child policy's name does not equal child_policy_name,
  //       we create a new policy.  The policy will be stored in
  //       pending_child_policy_ and will later be swapped into
  //       child_policy_ by the helper when the new child transitions
  //       into state READY.
  // 3. If we have both a child policy and a pending child policy (i.e.,
  //    we created a new policy for a previous update that has not yet
  //    been moved from pending_child_policy_ to child_policy_), then:
  //    a. If the pending child policy's name equals child_policy_name,
  //       then we update the existing pending child policy.
  //    b. If the pending child policy's name does not equal
  //       child_policy_name, then we create a new policy.  The new
  //       policy is stored in pending_child_policy_ (replacing the one
  //       that was there before, which will be immediately shut down)
  //       and will later be swapped into child_policy_ by the helper
  //       when the new child transitions into state READY.
  const char* child_policy_name =
      child_policy_name_ == nullptr ? "round_robin" : child_policy_name_.get();
  const bool create_policy =
      child_policy_ == nullptr ||
      (strcmp(child_policy_->name(), child_policy_name) != 0 &&
       (pending_child_policy_ == nullptr ||
        strcmp(pending_child_policy_->name(), child_policy_name) != 0));
  LoadBalancingPolicy* policy = nullptr;
  if (create_policy) {
    // Cases 1, 2b, and 3b: create a new child policy.
    // If child_policy_ is null, we set it (case 1), else we set
    // pending_child_policy_ (cases 2b and 3b).
    auto& lb_policy =
        child_policy_ == nullptr ? child_policy_ : pending_child_policy_;
    lb_policy = CreateChildPolicyLocked(child_policy_name, args);
    policy = lb_policy.get();
  } else {
    // Cases 2a and 3a: update an existing policy.
    // If we have a pending child policy, send the update to the pending
    // policy (case 3a), else send it to the current policy (case 2a).
    policy = pending_child_policy_ != nullptr ? pending_child_policy_.get()
                                              : child_policy_.get();
  }
  // Update the policy.
  if (policy != nullptr) {
    if (grpc_lb_glb_trace.enabled()) {
      gpr_log(GPR_INFO, "[grpclb %p] Updating child policy %p", this, policy);
    }
    policy->UpdateLocked(*args, child_policy_config_);
  }
  // Clean up.
  grpc_channel_args_destroy(args);
}

//
// factory
//

class GrpcLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    /* Count the number of gRPC-LB addresses. There must be at least one. */
    const ServerAddressList* addresses =
        FindServerAddressListChannelArg(args.args);
    if (addresses == nullptr) return nullptr;
    bool found_balancer = false;
    for (size_t i = 0; i < addresses->size(); ++i) {
      if ((*addresses)[i].IsBalancer()) {
        found_balancer = true;
        break;
      }
    }
    if (!found_balancer) return nullptr;
    return OrphanablePtr<LoadBalancingPolicy>(New<GrpcLb>(std::move(args)));
  }

  const char* name() const override { return kGrpclb; }
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
