//
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
#include <mutex>
#include <unordered_map>

#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/iomgr/timer.h"

namespace grpc_core {

class RlsLbConfig;

class RlsLb : public LoadBalancingPolicy {
 public:
  /// Map of key values extracted by builders from the RPC initial metadata.
  using KeyMap = std::map<std::string, std::string>;

  /// A KeyMapBuilder accepts a config that specifies how the keys should be
  /// built, then generate key map for calls based on their initial metadata.
  class KeyMapBuilder {
   public:
    KeyMapBuilder(const Json& config, grpc_error** error);

    KeyMap BuildKeyMap(const MetadataInterface* initial_metadata) const;

   private:
    std::map<std::string /*key_name*/, std::vector<std::string /*header_name*/>>
        pattern_;
  };

  /// A map from path name to a KeyMapBuilder instance that corresponds to that
  /// path. Note that by the design of the RLS system, the method portion of a
  /// path can be empty, which is considered to be a wildcard and matches any
  /// method in a service.
  using KeyMapBuilderMap = std::map<std::string /*path*/, KeyMapBuilder>;

  explicit RlsLb(Args args);

  // Implementation of LoadBalancingPolicy methods
  const char* name() const override;
  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  // Key to access entries in the cache and the request map.
  struct RequestKey {
    std::string path;
    KeyMap key_map;

    bool operator==(const RequestKey& rhs) const {
      return (path == rhs.path && key_map == rhs.key_map);
    }

    template <typename H>
    friend H AbslHashValue(H h, const RequestKey& key) {
      std::hash<std::string> string_hasher;
      for (auto& kv : key.key_map) {
        h = H::combine(std::move(h), string_hasher(kv.first),
                       string_hasher(kv.second));
      }
      return H::combine(std::move(h), string_hasher(key.path));
    }

    size_t Size() const {
      size_t size = path.length() + sizeof(RequestKey);
      for (auto& kv : key_map) {
        size += (kv.first.length() + kv.second.length());
      }
      return size;
    }
  };

  struct ResponseInfo {
    grpc_error* error;
    std::vector<std::string> targets;
    std::string header_data;
  };

  /// A stateless picker that only contains a reference to the RLS lb policy
  /// that created it. When processing a pick, it depends on the current state
  /// of the lb policy to make a decision.
  class Picker : public LoadBalancingPolicy::SubchannelPicker {
   public:
    explicit Picker(RefCountedPtr<RlsLb> lb_policy) : lb_policy_(lb_policy) {}

    PickResult Pick(PickArgs args) override;

   private:
    RefCountedPtr<RlsLb> lb_policy_;
  };

  class ChildPolicyWrapper : public InternallyRefCounted<ChildPolicyWrapper> {
   public:
    ChildPolicyWrapper(RefCountedPtr<RlsLb> lb_policy, std::string target)
        : lb_policy_(lb_policy),
          target_(std::move(target)),
          picker_(absl::make_unique<QueuePicker>(std::move(lb_policy))) {}

    /// Pick subchannel for call. If the picker is not reported by the child
    /// policy (i.e. picker_ == nullptr), the pick will be failed.
    PickResult Pick(PickArgs args);

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
    /// until the child policy reports another READY state. The mutex of the
    /// RLS LB policy should be held when calling this method.
    grpc_connectivity_state ConnectivityState() const;

    void Orphan() override;

    const std::string& target() const { return target_; }

   private:
    /// ChannelControlHelper object that allows the child policy to update state
    /// with the wrapper.
    class ChildPolicyHelper : public LoadBalancingPolicy::ChannelControlHelper {
     public:
      explicit ChildPolicyHelper(RefCountedPtr<ChildPolicyWrapper> wrapper)
          : wrapper_(wrapper) {}

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
      RefCountedPtr<ChildPolicyWrapper> wrapper_;
    };

    RefCountedPtr<RlsLb> lb_policy_;
    std::string target_;

    bool is_shutdown_ = false;

    /// Whether the child policy state has been in TRANSIENT_FAILURE since last
    /// READY state.
    bool was_transient_failure_ = false;
    OrphanablePtr<ChildPolicyHandler> child_policy_;
    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
    std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker_;
  };

  /// Class that allows multiple ownership of the child policy wrapper object.
  /// The child policy wrapper is orphaned when all the owners remove their
  /// references.
  class ChildPolicyOwner : public RefCounted<ChildPolicyOwner> {
   public:
    ChildPolicyOwner(OrphanablePtr<ChildPolicyWrapper> child, RlsLb* parent);
    ~ChildPolicyOwner();

    ChildPolicyWrapper* child() const { return child_.get(); }

   private:
    RlsLb* parent_;
    OrphanablePtr<ChildPolicyWrapper> child_;
  };

  /// An LRU cache with adjustable size.
  class Cache {
   public:
    using Iterator = std::list<RequestKey>::iterator;

    class Entry : public InternallyRefCounted<Entry> {
     public:
      explicit Entry(RefCountedPtr<RlsLb> lb_policy);

      /// Pick subchannel for request based on the entry's state.
      PickResult Pick(PickArgs args);

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
                               std::unique_ptr<BackOff> backoff_state);

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

      std::vector<RefCountedPtr<ChildPolicyOwner>> child_policy_wrappers_;
      std::string header_data_;
      grpc_millis data_expiration_time_ = GRPC_MILLIS_INF_PAST;
      grpc_millis stale_time_ = GRPC_MILLIS_INF_PAST;

      grpc_millis min_expiration_time_ = GRPC_MILLIS_INF_PAST;
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
    /// the size limit.
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
      Throttle(int window_size_seconds = 0, double ratio_for_successes = 0,
               int paddings = 0);

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
    ~RlsRequest();

    /// Shutdown the entry. Cancel the RLS call on the fly if applicable. After
    /// shutdown, further call responses are no longer reported to the lb
    /// policy.
    void Orphan() override;

   private:
    static void StartCall(void* arg, grpc_error* error);

    /// Callback to be called by core when the call is completed.
    static void OnRlsCallComplete(void* arg, grpc_error* error);

    /// Call completion callback running on lb policy combiner.
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

  using ChildPolicyMap = std::map<std::string, ChildPolicyOwner*>;

  void ShutdownLocked() override;

  /// Find key builder map corresponding to path. The function will lookup for
  /// both exact match and wildcard match. If the corresponding key builder is
  /// not found, return nullptr.
  const KeyMapBuilder* FindKeyMapBuilder(const std::string& path) const;

  /// The method checks if there is already an RLS call pending for the key. If
  /// not, the method further checks if a new RLS call should be throttle. If
  /// not, an RLS call is made.
  ///
  /// The method returns false if a new RLS call is throttled; otherwise it
  /// returns true.
  bool MaybeMakeRlsCall(const RequestKey& key,
                        std::unique_ptr<BackOff>* backoff_state = nullptr);

  /// Update picker with the channel to trigger reprocessing of pending picks.
  /// The method will schedule the actual picker update on the execution context
  /// to be run later.
  void UpdatePicker();

  /// Update picker in the LB policy's work serializer.
  static void UpdatePickerCallback(void* arg, grpc_error* error);

  /// The name of the server for the channel.
  std::string server_name_;

  /// Mutex that protects the states of the lb policy which are shared with the
  /// picker, including cache_, request_map_, channel_, and
  /// default_child_policy_.
  std::recursive_mutex mu_;
  bool is_shutdown_ = false;

  RefCountedPtr<RlsLbConfig> config_;
  ServerAddressList addresses_;
  const grpc_channel_args* channel_args_ = nullptr;
  Cache cache_;
  RequestMap request_map_;
  RefCountedPtr<ControlChannel> channel_;
  ChildPolicyMap child_policy_map_;
  RefCountedPtr<ChildPolicyOwner> default_child_policy_;
};

/// Parsed RLS LB policy configuration.
class RlsLbConfig : public LoadBalancingPolicy::Config {
 public:
  RlsLbConfig(RlsLb::KeyMapBuilderMap key_map_builder_map,
              std::string lookup_service, grpc_millis lookup_service_timeout,
              grpc_millis max_age, grpc_millis stale_age,
              int64_t cache_size_bytes, std::string default_target,
              Json child_policy_config,
              RefCountedPtr<LoadBalancingPolicy::Config>
                  default_child_policy_parsed_config,
              std::string child_policy_config_target_field_name)
      : key_map_builder_map_(std::move(key_map_builder_map)),
        lookup_service_(std::move(lookup_service)),
        lookup_service_timeout_(lookup_service_timeout),
        max_age_(max_age),
        stale_age_(stale_age),
        cache_size_bytes_(cache_size_bytes),
        default_target_(std::move(default_target)),
        child_policy_config_(std::move(child_policy_config)),
        default_child_policy_parsed_config_(
            std::move(default_child_policy_parsed_config)),
        child_policy_config_target_field_name_(
            std::move(child_policy_config_target_field_name)) {}

  const char* name() const override;

  const RlsLb::KeyMapBuilderMap& key_map_builder_map() const {
    return key_map_builder_map_;
  }

  const std::string& lookup_service() const { return lookup_service_; }

  grpc_millis lookup_service_timeout() const { return lookup_service_timeout_; }

  grpc_millis max_age() const { return max_age_; }

  grpc_millis stale_age() const { return stale_age_; }

  int64_t cache_size_bytes() const { return cache_size_bytes_; }

  const std::string& default_target() const { return default_target_; }

  const Json& child_policy_config() const { return child_policy_config_; }

  RefCountedPtr<LoadBalancingPolicy::Config>
  default_child_policy_parsed_config() const {
    return default_child_policy_parsed_config_;
  }

  const std::string& child_policy_config_target_field_name() const {
    return child_policy_config_target_field_name_;
  }

 private:
  RlsLb::KeyMapBuilderMap key_map_builder_map_;
  std::string lookup_service_;
  grpc_millis lookup_service_timeout_;
  grpc_millis max_age_;
  grpc_millis stale_age_;
  int64_t cache_size_bytes_;
  std::string default_target_;
  Json child_policy_config_;
  RefCountedPtr<LoadBalancingPolicy::Config>
      default_child_policy_parsed_config_;
  std::string child_policy_config_target_field_name_;
};

class RlsLbFactory : public LoadBalancingPolicyFactory {
 public:
  const char* name() const override;

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override;

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& config, grpc_error** error) const override;
};

/// Build a key map builder map from RLS configuration.
RlsLb::KeyMapBuilderMap RlsCreateKeyMapBuilderMap(const Json& config,
                                                  grpc_error** error);

/// Find key builder map corresponding to a path in key_map_builder_map. The
/// function will lookup for both exact match and wildcard match. If the
/// corresponding key builder is not found, return nullptr.
const RlsLb::KeyMapBuilder* RlsFindKeyMapBuilder(
    const RlsLb::KeyMapBuilderMap& key_map_builder_map,
    const std::string& path);

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_RLS_RLS_H
