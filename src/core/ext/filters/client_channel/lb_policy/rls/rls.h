/*
 *
 * Copyright 2020 gRPC authors.
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
#include <mutex>
#include <unordered_map>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/iomgr/timer.h"

namespace grpc_core {

class RlsLbConfig;
class RlsLbFactory;

class RlsLb : public LoadBalancingPolicy {
 public:
  using KeyMap = std::unordered_map<std::string, std::string>;

  class KeyMapBuilder {
   public:
    KeyMapBuilder(const Json& config, grpc_error** error);

    KeyMap BuildKeyMap(const MetadataInterface* initial_metadata) const;

   private:
    std::unordered_map<std::string, std::vector<std::pair<std::string, int>>>
        pattern_;
  };

  using KeyMapBuilderMap = std::unordered_map<std::string, KeyMapBuilder>;

  enum class RequestProcessingStrategy {
    /// Query the RLS and process the request using target returned by the
    /// lookup. The target will then be cached and used for processing
    /// subsequent requests for the same key. Any errors during lookup service
    /// processing will fall back to default target for request processing.
    SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR = 0,

    /// Query the RLS and process the request using target returned by the
    /// lookup. The target will then be cached and used for processing
    /// subsequent requests for the same key. Any errors during lookup service
    /// processing will return an error back to the client.  Services with
    /// strict regional routing requirements should use this strategy.
    SYNC_LOOKUP_CLIENT_SEES_ERROR = 1,

    /// Query the RLS asynchronously but respond with the default target.  The
    /// target in the lookup response will then be cached and used for
    /// subsequent requests.  Services with strict latency requirements (but not
    /// strict regional routing requirements) should use this strategy.
    ASYNC_LOOKUP_DEFAULT_TARGET_ON_MISS = 2,

    STRATEGY_UNSPECIFIED = 3,
  };

  explicit RlsLb(Args args);

  // Implementation of LoadBalancingPolicy methods
  const char* name() const override;
  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  class ChildPolicyWrapper;

  class RequestMapEntry;

  // Key to access entries in the cache and the request map.
  struct Key {
    std::string path;
    KeyMap key_map;

    bool operator==(const Key& rhs) const;
  };

  class KeyHasher {
   public:
    size_t operator()(const Key& key) const;
  };

  using RequestMap =
      std::unordered_map<Key, OrphanablePtr<RequestMapEntry>, KeyHasher>;

  struct ResponseInfo {
    grpc_error* error;
    std::string target;
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
    /// Handler class that allows multiple owners of the child policy
    /// wrapper object and shutdown of the child policy wrapper object at the
    /// same time.
    class RefHandler : public RefCounted<RefHandler> {
     public:
      RefHandler(ChildPolicyWrapper* child) : child_(child) {}

      ChildPolicyWrapper* child() const;

     private:
      OrphanablePtr<ChildPolicyWrapper> child_;
    };

    ChildPolicyWrapper(RefCountedPtr<RlsLb> lb_policy, std::string target)
        : lb_policy_(std::move(lb_policy)), target_(std::move(target)) {}

    /// Pick subchannel for call. If the picker is not reported by the child
    /// policy (i.e. picker_ == nullptr), the pick will be failed.
    PickResult Pick(PickArgs args);

    /// Checks if the child policy is ready to process a pick request (i.e. the
    /// picker is not nullptr).
    bool IsReady() const;

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
          const grpc_channel_args& args) override;
      void UpdateState(grpc_connectivity_state state,
                       std::unique_ptr<SubchannelPicker> picker) override;
      void RequestReresolution() override;
      void AddTraceEvent(TraceSeverity severity, StringView message) override;

     private:
      RefCountedPtr<ChildPolicyWrapper> wrapper_;
    };

    RefCountedPtr<RlsLb> lb_policy_;
    std::string target_;

    bool is_shutdown_ = false;

    OrphanablePtr<ChildPolicyHandler> child_policy_;
    grpc_connectivity_state connectivity_state_;
    std::unique_ptr<LoadBalancingPolicy::SubchannelPicker> picker_;
  };

  /// An LRU cache with adjustable size.
  class Cache {
   public:
    using Iterator = std::list<Key>::iterator;

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

      /// Updates the entry upon reception of a new RLS response.
      void OnRlsResponseLocked(ResponseInfo response,
                               std::unique_ptr<BackOff> backoff_state);

      /// Set the iterator to the lru_list element of the cache corresponding to
      /// this entry.
      void set_iterator(Cache::Iterator iterator);
      /// Get the iterator to the lru_list element of the cache corresponding to
      /// this entry.
      Cache::Iterator iterator() const;

     private:
      static void OnBackoffTimerLocked(void* args, grpc_error* error);

      // Callback when the backoff timer is fired.
      static void OnBackoffTimer(void* args, grpc_error* error);

      RefCountedPtr<RlsLb> lb_policy_;

      bool is_shutdown_ = false;

      // Backoff states

      grpc_error* status_ = GRPC_ERROR_NONE;
      std::unique_ptr<BackOff> backoff_state_ = nullptr;
      grpc_timer backoff_timer_;
      bool timer_pending_ = false;
      grpc_closure backoff_timer_callback_;
      grpc_closure backoff_timer_combiner_callback_;
      grpc_millis backoff_time_ = GRPC_MILLIS_INF_PAST;
      grpc_millis backoff_expiration_time_ = GRPC_MILLIS_INF_PAST;

      // RLS response states

      RefCountedPtr<ChildPolicyWrapper::RefHandler> child_policy_wrapper_ =
          nullptr;
      std::string header_data_;
      grpc_millis data_expiration_time_ = GRPC_MILLIS_INF_PAST;
      grpc_millis stale_time_ = GRPC_MILLIS_INF_PAST;

      grpc_millis min_expiration_time_ = GRPC_MILLIS_INF_PAST;
      Cache::Iterator lru_iterator_;
    };

    explicit Cache(RlsLb* lb_policy);

    /// Find an entry from the cache that corresponds to a key. If an entry is
    /// not found, nullptr is returned. Otherwise, the entry's usage is updated.
    Entry* Find(Key key);

    /// Add an entry to the cache. If an entry with the same key exists, the
    /// method is a no-op.
    void Add(Key key, OrphanablePtr<Entry> entry);

    /// Resize the cache. If the new cache size is greater than the current size
    /// of the cache, do nothing. Otherwise, evict the oldest entries that
    /// exceed the new size limit of the cache.
    void Resize(int64_t bytes);

    /// Resets backoff of all the cache entries.
    void ResetAllBackoff();

    /// Shutdown the cache; clean-up and orphan all the stored cache entries.
    void Shutdown();

   private:
    static void OnCleanupTimer(void* arg, grpc_error* error);

    RlsLb* lb_policy_;

    int64_t size_bytes_ = 0;
    int element_size_ = 0;

    std::list<Key> lru_list_;
    std::unordered_map<Key, OrphanablePtr<Entry>, KeyHasher> map_;
    grpc_timer cleanup_timer_;
    grpc_closure timer_callback_;

    const int kCacheEntrySize =
        (2 * sizeof(Key) + sizeof(Entry) + sizeof(OrphanablePtr<Entry>));
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
    bool ShouldThrottle();

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
      uint32_t rng_state_;

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
      void OnConnectivityStateChange(
          grpc_connectivity_state new_state) override;

      static void OnReadyLocked(void* arg, grpc_error* error);

      RefCountedPtr<ControlChannel> channel_;

      bool was_transient_failure_ = false;
      grpc_closure on_ready_locked_cb_;
    };

    RefCountedPtr<RlsLb> lb_policy_;
    bool is_shutdown_ = false;

    grpc_channel* channel_ = nullptr;
    Throttle throttle_;
    StateWatcher* watcher_ = nullptr;
  };

  /// An entry in the request map that handles the state of an RLS call
  /// corresponding to a particular RLS request.
  class RequestMapEntry : public InternallyRefCounted<RequestMapEntry> {
   public:
    /// Creates the entry. Make a call on channel with the fields of the request
    /// coming from key.
    RequestMapEntry(RefCountedPtr<RlsLb> lb_policy, RlsLb::Key key,
                    RefCountedPtr<ControlChannel> channel,
                    std::unique_ptr<BackOff> backoff_state);
    ~RequestMapEntry();

    /// Shutdown the entry. Cancel the RLS call on the fly if applicable. After
    /// shutdown, further call responses are no longer reported to the lb
    /// policy.
    void Orphan() override;

   private:
    struct Response {
      grpc_error* error;
      std::string target;
      std::string header_data;
    };

    static void StartCall(void* arg, grpc_error* error);

    /// Callback to be called by core when the call is completed.
    static void OnRlsCallComplete(void* arg, grpc_error* error);

    /// Call completion callback running on lb policy combiner.
    static void OnRlsCallCompleteLocked(void* arg, grpc_error* error);

    void MakeRequestProto();

    grpc_error* ParseResponseProto(std::string* target,
                                   std::string* header_data);

    RefCountedPtr<RlsLb> lb_policy_;
    RlsLb::Key key_;
    RefCountedPtr<ControlChannel> channel_;

    std::unique_ptr<BackOff> backoff_state_;

    // RLS call related variables and states

    grpc_closure call_start_cb_;
    grpc_closure call_complete_cb_;
    grpc_closure call_complete_locked_cb_;
    grpc_call* call_ = nullptr;
    grpc_byte_buffer* message_send_ = nullptr;
    grpc_metadata_array initial_metadata_recv_;
    grpc_byte_buffer* message_recv_ = nullptr;
    grpc_metadata_array trailing_metadata_recv_;
    grpc_status_code status_recv_;
    grpc_slice status_details_recv_;
  };

  using ChildPolicyMap =
      std::unordered_map<std::string, ChildPolicyWrapper::RefHandler*>;

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
  bool MaybeMakeRlsCall(const Key& key,
                        std::unique_ptr<BackOff>* backoff_state = nullptr);

  /// Update picker with the channel to trigger reprocessing of pending picks.
  /// The method must be invoked on the control plane combiner.
  void UpdatePickerLocked();

  std::string server_uri_;

  /// Mutex that protects the entire state of the LB policy, which includes all
  /// the states below.
  std::recursive_mutex mu_;
  bool is_shutdown_ = false;

  RefCountedPtr<RlsLbConfig> current_config_;
  ServerAddressList current_addresses_;
  grpc_channel_args* current_channel_args_ = nullptr;
  Cache cache_;
  RequestMap request_map_;
  RefCountedPtr<ControlChannel> channel_;
  ChildPolicyMap child_policy_map_;
  RefCountedPtr<ChildPolicyWrapper::RefHandler> default_child_policy_;
};

/// Parsed RLS LB policy configuration.
class RlsLbConfig : public LoadBalancingPolicy::Config {
 public:
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

  RlsLb::RequestProcessingStrategy request_processing_strategy() const {
    return request_processing_strategy_;
  }

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

  RlsLb::RequestProcessingStrategy request_processing_strategy_;

  Json child_policy_config_;

  RefCountedPtr<LoadBalancingPolicy::Config>
      default_child_policy_parsed_config_;

  std::string child_policy_config_target_field_name_;

  friend RlsLbFactory;
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

/// Extract path of the call from metadata.
std::string RlsFindPathFromMetadata(
    LoadBalancingPolicy::MetadataInterface* metadata);

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_RLS_RLS_H
