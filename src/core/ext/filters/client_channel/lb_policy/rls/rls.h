//
// Copyright 2020 gRPC authors.
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

/// Implementation of the Route Lookup Service (RLS) LB policy
///
/// The policy queries a route lookup service for the name of the actual service
/// to use. A child policy that recognizes the name as a field of its
/// configuration will take further load balancing action on the request.

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_RLS_RLS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_RLS_RLS_H

#include <grpc/support/port_platform.h>

#include <deque>
#include <list>
#include <map>
#include <unordered_map>

#include "absl/hash/hash.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/iomgr/timer.h"

namespace grpc_core {

/// Parsed RLS LB policy configuration.
class RlsLbConfig : public LoadBalancingPolicy::Config {
 public:
  struct KeyBuilder {
    std::map<std::string /*key*/, std::vector<std::string /*header*/>>
        header_keys;
    std::string host_key;
    std::string service_key;
    std::string method_key;
    std::map<std::string /*key*/, std::string /*value*/> constant_keys;
  };
  using KeyBuilderMap = std::unordered_map<std::string /*path*/, KeyBuilder>;

  struct RouteLookupConfig {
    KeyBuilderMap key_builder_map;
    std::string lookup_service;
    grpc_millis lookup_service_timeout = 0;
    grpc_millis max_age = 0;
    grpc_millis stale_age = 0;
    int64_t cache_size_bytes = 0;
    std::string default_target;
  };

  RlsLbConfig(RouteLookupConfig route_lookup_config, Json child_policy_config,
              std::string child_policy_config_target_field_name,
              RefCountedPtr<LoadBalancingPolicy::Config>
                  default_child_policy_parsed_config)
      : route_lookup_config_(std::move(route_lookup_config)),
        child_policy_config_(std::move(child_policy_config)),
        child_policy_config_target_field_name_(
            std::move(child_policy_config_target_field_name)),
        default_child_policy_parsed_config_(
            std::move(default_child_policy_parsed_config)) {}

  const char* name() const override;

  const KeyBuilderMap& key_builder_map() const {
    return route_lookup_config_.key_builder_map;
  }
  const std::string& lookup_service() const {
    return route_lookup_config_.lookup_service;
  }
  grpc_millis lookup_service_timeout() const {
    return route_lookup_config_.lookup_service_timeout;
  }
  grpc_millis max_age() const { return route_lookup_config_.max_age; }
  grpc_millis stale_age() const { return route_lookup_config_.stale_age; }
  int64_t cache_size_bytes() const {
    return route_lookup_config_.cache_size_bytes;
  }
  const std::string& default_target() const {
    return route_lookup_config_.default_target;
  }
  const Json& child_policy_config() const { return child_policy_config_; }
  const std::string& child_policy_config_target_field_name() const {
    return child_policy_config_target_field_name_;
  }
  RefCountedPtr<LoadBalancingPolicy::Config>
  default_child_policy_parsed_config() const {
    return default_child_policy_parsed_config_;
  }

 private:
  RouteLookupConfig route_lookup_config_;
  Json child_policy_config_;
  std::string child_policy_config_target_field_name_;
  RefCountedPtr<LoadBalancingPolicy::Config>
      default_child_policy_parsed_config_;
};

class RlsLb : public LoadBalancingPolicy {
 public:
  explicit RlsLb(Args args);

  // Implementation of LoadBalancingPolicy methods
  const char* name() const override;
  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  // Key to access entries in the cache and the request map.
  struct RequestKey {
    std::map<std::string, std::string> key_map;

    bool operator==(const RequestKey& rhs) const {
      return key_map == rhs.key_map;
    }

    template <typename H>
    friend H AbslHashValue(H h, const RequestKey& key) {
      std::hash<std::string> string_hasher;
      for (auto& kv : key.key_map) {
        h = H::combine(std::move(h), string_hasher(kv.first),
                       string_hasher(kv.second));
      }
      return h;
    }

    size_t Size() const {
      size_t size = sizeof(RequestKey);
      for (auto& kv : key_map) {
        size += kv.first.length() + kv.second.length();
      }
      return size;
    }

    std::string ToString() const {
      return absl::StrCat(
          "{", absl::StrJoin(key_map, ",", absl::PairFormatter("=")), "}");
    }
  };

  struct ResponseInfo {
    grpc_error* error;
    std::vector<std::string> targets;
    std::string header_data;
  };

  class ChildPolicyWrapper : public DualRefCounted<ChildPolicyWrapper> {
   public:
    ChildPolicyWrapper(RefCountedPtr<RlsLb> lb_policy, std::string target)
        : lb_policy_(lb_policy),
          target_(std::move(target)),
          picker_(absl::make_unique<QueuePicker>(std::move(lb_policy))) {}

    /// Pick subchannel for call. If the picker is not reported by the child
    /// policy (i.e. picker_ == nullptr), the pick will be failed.
    PickResult Pick(PickArgs args) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&RlsLb::mu_);

    /// Validate the configuration of the child policy with the extra target
    /// name field. If the child policy configuration does not validate, a
    /// TRANSIENT_FAILURE picker is returned. Otherwise, the child policy is
    /// updated with the new configuration.
    ///
    /// Does not transfer ownership of channel_args
    void UpdateLocked(const Json& child_policy_config,
                      ServerAddressList addresses,
                      const grpc_channel_args* channel_args);

    /// The method is directly forwarded to ExitIdleLocked() method of the child
    /// policy
    void ExitIdleLocked();

    /// The method is directly forwarded to ResetBackoffLocked() method of the
    /// child policy
    void ResetBackoffLocked();

    /// Get the connectivity state of the child policy. Once the child policy
    /// reports TRANSIENT_FAILURE, the function will always return
    /// TRANSIENT_FAILURE state instead of the actual state of the child policy
    /// until the child policy reports another READY state.
    grpc_connectivity_state connectivity_state() const
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&RlsLb::mu_) {
      return connectivity_state_;
    }

    void Orphan() override;

    const std::string& target() const { return target_; }

   private:
    /// ChannelControlHelper object that allows the child policy to update state
    /// with the wrapper.
    class ChildPolicyHelper : public LoadBalancingPolicy::ChannelControlHelper {
     public:
      explicit ChildPolicyHelper(WeakRefCountedPtr<ChildPolicyWrapper> wrapper)
          : wrapper_(std::move(wrapper)) {}

      // Implementation of ChannelControlHelper interface.
      RefCountedPtr<SubchannelInterface> CreateSubchannel(
          ServerAddress address, const grpc_channel_args& args) override;
      void UpdateState(grpc_connectivity_state state,
                       const absl::Status& status,
                       std::unique_ptr<SubchannelPicker> picker) override;
      void RequestReresolution() override;
      void AddTraceEvent(TraceSeverity severity,
                         absl::string_view message) override;

     private:
      WeakRefCountedPtr<ChildPolicyWrapper> wrapper_;
    };

    RefCountedPtr<RlsLb> lb_policy_;
    std::string target_;

    bool is_shutdown_ = false;

    OrphanablePtr<ChildPolicyHandler> child_policy_;
    grpc_connectivity_state connectivity_state_ ABSL_GUARDED_BY(&RlsLb::mu_) =
        GRPC_CHANNEL_IDLE;
    std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker_;
  };

  /// A stateless picker that only contains a reference to the RLS lb policy
  /// that created it. When processing a pick, it depends on the current state
  /// of the lb policy to make a decision.
  class Picker : public LoadBalancingPolicy::SubchannelPicker {
   public:
    explicit Picker(RefCountedPtr<RlsLb> lb_policy)
        : lb_policy_(std::move(lb_policy)),
          config_(lb_policy_->config_),
          default_child_policy_(lb_policy_->default_child_policy_) {}

    PickResult Pick(PickArgs args) override;

   private:
    RefCountedPtr<RlsLb> lb_policy_;
    RefCountedPtr<RlsLbConfig> config_;
    RefCountedPtr<ChildPolicyWrapper> default_child_policy_;
  };

  /// An LRU cache with adjustable size.
  class Cache {
   public:
    using Iterator = std::list<RequestKey>::iterator;

    class Entry : public InternallyRefCounted<Entry> {
     public:
      explicit Entry(RefCountedPtr<RlsLb> lb_policy);

      /// Pick subchannel for request based on the entry's state.
      PickResult Pick(PickArgs args, RlsLbConfig* config,
                      ChildPolicyWrapper* default_child_policy)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&RlsLb::mu_);

      /// If the cache entry is in backoff state, resets the backoff and, if
      /// applicable, its backoff timer. The method does not update the LB
      /// policy's picker; the caller is responsible for that if necessary.
      void ResetBackoff();

      /// Check if the entry should be removed by the clean-up timer.
      bool ShouldRemove() const;

      /// Check if the entry can be evicted from the cache, i.e. the
      /// min_expiration_time_ has passed.
      bool CanEvict() const;

      /// Notify the entry when it's evicted from the cache. Performs shut down.
      void Orphan() override;

      /// Updates the entry upon reception of a new RLS response. This method
      /// must be called from the LB policy work serializer.
      void OnRlsResponseLocked(ResponseInfo response,
                               std::unique_ptr<BackOff> backoff_state)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&RlsLb::mu_);

      /// Set the iterator to the lru_list element of the cache corresponding to
      /// this entry.
      void set_iterator(Cache::Iterator iterator) { lru_iterator_ = iterator; }
      /// Get the iterator to the lru_list element of the cache corresponding to
      /// this entry.
      Cache::Iterator iterator() const { return lru_iterator_; }

     private:
      /// Callback when the backoff timer is fired.
      static void OnBackoffTimer(void* args, grpc_error* error);

      RefCountedPtr<RlsLb> lb_policy_;

      bool is_shutdown_ = false;

      // Backoff states
      grpc_error* status_ = GRPC_ERROR_NONE;
      std::unique_ptr<BackOff> backoff_state_ = nullptr;
      grpc_timer backoff_timer_;
      bool timer_pending_ = false;
      grpc_closure backoff_timer_callback_;
      grpc_millis backoff_time_ = GRPC_MILLIS_INF_PAST;
      grpc_millis backoff_expiration_time_ = GRPC_MILLIS_INF_PAST;

      // RLS response states
      std::vector<RefCountedPtr<ChildPolicyWrapper>> child_policy_wrappers_;
      std::string header_data_;
      grpc_millis data_expiration_time_ = GRPC_MILLIS_INF_PAST;
      grpc_millis stale_time_ = GRPC_MILLIS_INF_PAST;

      grpc_millis min_expiration_time_;
      Cache::Iterator lru_iterator_;
    };

    explicit Cache(RlsLb* lb_policy);

    /// Find an entry from the cache that corresponds to a key. If an entry is
    /// not found, nullptr is returned. Otherwise, the entry is considered
    /// recently used and its order in the LRU list of the cache is updated.
    Entry* Find(const RequestKey& key);

    /// Find an entry from the cache that corresponds to a key. If an entry is
    /// not found, an entry is created, inserted in the cache, and returned to
    /// the caller. Otherwise, the entry found is returned to the caller. The
    /// entry returned to the user is considered recently used and its order in
    /// the LRU list of the cache is updated.
    Entry* FindOrInsert(const RequestKey& key);

    /// Resize the cache. If the new cache size is greater than the current size
    /// of the cache, do nothing. Otherwise, evict the oldest entries that
    /// exceed the new size limit of the cache.
    void Resize(int64_t bytes);

    /// Resets backoff of all the cache entries.
    void ResetAllBackoff();

    /// Shutdown the cache; clean-up and orphan all the stored cache entries.
    void Shutdown();

   private:
    using MapType = std::unordered_map<RequestKey, OrphanablePtr<Entry>,
                                       absl::Hash<RequestKey>>;

    static void OnCleanupTimer(void* arg, grpc_error* error);

    /// Evict oversized cache elements when the current size is greater than
    /// the specified limit.
    void MaybeShrinkSize(int64_t bytes);

    /// Set an entry to be recently used and move it to the end of the LRU
    /// list.
    void SetRecentUsage(MapType::iterator entry);

    RlsLb* lb_policy_;

    int64_t size_limit_ = 0;
    int64_t size_ = 0;

    std::list<RequestKey> lru_list_;
    MapType map_;
    grpc_timer cleanup_timer_;
    grpc_closure timer_callback_;
  };

  class ControlChannel : public InternallyRefCounted<ControlChannel> {
   public:
    ControlChannel(RefCountedPtr<RlsLb> lb_policy, const std::string& target,
                   const grpc_channel_args* channel_args);

    /// Disconnect the channel and clean-up.
    void Orphan() override;

    /// Report the result of an RLS call to the throttle.
    void ReportResponseLocked(bool response_succeeded);

    /// Check if a proposed RLS call should be throttled.
    bool ShouldThrottle() { return throttle_.ShouldThrottle(); }

    /// Resets the channel's backoff.
    void ResetBackoff();

    grpc_channel* channel() const { return channel_; }

   private:
    class Throttle {
     public:
      explicit Throttle(int window_size_seconds = 0,
                        double ratio_for_successes = 0, int paddings = 0);

      bool ShouldThrottle();

      void RegisterResponse(bool success);

     private:
      grpc_millis window_size_;
      double ratio_for_successes_;
      int paddings_;

      // Logged timestamp of requests
      std::deque<grpc_millis> requests_;

      // Logged timestamp of responses that were successful.
      std::deque<grpc_millis> successes_;
    };

    /// Watcher to the state of the RLS control channel. Notifies the LB policy
    /// when the channel was previously in TRANSIENT_FAILURE and then becomes
    /// READY.
    class StateWatcher : public AsyncConnectivityStateWatcherInterface {
     public:
      explicit StateWatcher(RefCountedPtr<ControlChannel> channel);

     private:
      void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                     const absl::Status& status) override;

      RefCountedPtr<ControlChannel> channel_;
      bool was_transient_failure_ = false;
    };

    RefCountedPtr<RlsLb> lb_policy_;
    bool is_shutdown_ = false;

    grpc_channel* channel_ = nullptr;
    Throttle throttle_;
    StateWatcher* watcher_ = nullptr;
  };

  /// An entry in the request map that handles the state of an RLS call
  /// corresponding to a particular RLS request.
  class RlsRequest : public InternallyRefCounted<RlsRequest> {
   public:
    /// Creates the entry. Make a call on channel with the fields of the request
    /// coming from key.
    RlsRequest(RefCountedPtr<RlsLb> lb_policy, RlsLb::RequestKey key,
               RefCountedPtr<ControlChannel> channel,
               std::unique_ptr<BackOff> backoff_state);
    ~RlsRequest() override;

    /// Shutdown the entry. Cancel the RLS call on the fly if applicable. After
    /// shutdown, further call responses are no longer reported to the lb
    /// policy.
    void Orphan() override;

   private:
    static void StartCall(void* arg, grpc_error* error);

    /// Callback to be called by core when the call is completed.
    static void OnRlsCallComplete(void* arg, grpc_error* error);

    /// Call completion callback running on lb policy WorkSerializer.
    void OnRlsCallCompleteLocked(grpc_error* error);

    grpc_byte_buffer* MakeRequestProto();

    ResponseInfo ParseResponseProto();

    RefCountedPtr<RlsLb> lb_policy_;
    RlsLb::RequestKey key_;
    RefCountedPtr<ControlChannel> channel_;

    std::unique_ptr<BackOff> backoff_state_;

    // RLS call related variables and states
    grpc_closure call_start_cb_;
    grpc_closure call_complete_cb_;
    grpc_call* call_ = nullptr;
    grpc_byte_buffer* send_message_ = nullptr;
    grpc_metadata_array recv_initial_metadata_;
    grpc_byte_buffer* recv_message_ = nullptr;
    grpc_metadata_array recv_trailing_metadata_;
    grpc_status_code status_recv_;
    grpc_slice status_details_recv_;
  };

  /// Maps an RLS request key to a RequestMap object that represents a pending
  /// RLS request.
  using RequestMap = std::unordered_map<RequestKey, OrphanablePtr<RlsRequest>,
                                        absl::Hash<RequestKey>>;

  using ChildPolicyMap = std::map<std::string, ChildPolicyWrapper*>;

  void ShutdownLocked() override;

  /// The method checks if there is already an RLS call pending for the key. If
  /// not, the method further checks if a new RLS call should be throttle. If
  /// not, an RLS call is made.
  ///
  /// The method returns false if a new RLS call is throttled; otherwise it
  /// returns true.
  bool MaybeMakeRlsCall(const RequestKey& key,
                        std::unique_ptr<BackOff>* backoff_state = nullptr)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  /// Update picker with the channel to trigger reprocessing of pending picks.
  /// The method will schedule the actual picker update on the execution context
  /// to be run later.
  void UpdatePicker();

  /// Update picker in the LB policy's work serializer.
  static void UpdatePickerCallback(void* arg, grpc_error* error);

  /// The name of the server for the channel.
  std::string server_name_;

  /// Mutex that protects the states of the LB policy that are shared with the
  /// picker.
  Mutex mu_;
  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;
  Cache cache_ ABSL_GUARDED_BY(mu_);

  // FIXME: hop into WorkSerializer to actually start RLS call, so that
  // only the map itself has to be guarded by the mutex
  RequestMap request_map_ ABSL_GUARDED_BY(mu_);

  // FIXME: see if there's a way to change things such that the throttle
  // object is covered by the mutex but the rest of the channel is not,
  // since that's the only part that the picker needs to access
  RefCountedPtr<ControlChannel> channel_ ABSL_GUARDED_BY(mu_);

  // Accessed only from within WorkSerializer.
  ServerAddressList addresses_;
  const grpc_channel_args* channel_args_ = nullptr;
  RefCountedPtr<RlsLbConfig> config_;
  RefCountedPtr<ChildPolicyWrapper> default_child_policy_;
  ChildPolicyMap child_policy_map_;
};

class RlsLbFactory : public LoadBalancingPolicyFactory {
 public:
  const char* name() const override;

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override;

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& config, grpc_error** error) const override;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_RLS_RLS_H
