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

#include <stdlib.h>
#include <algorithm>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "upb/upb.hpp"

#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/rls/rls.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/upb-generated/src/proto/grpc/lookup/v1/rls.upb.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_util.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

TraceFlag grpc_lb_rls_trace(false, "rls");

namespace {

const char* kRls = "rls";
const char kGrpc[] = "grpc";
const char* kRlsRequestPath = "/grpc.lookup.v1.RouteLookupService/RouteLookup";
const char* kDummyTargetFieldValue = "dummy_target_field_value";
const char* kRlsHeaderKey = "X-Google-RLS-Data";

const grpc_millis kDefaultLookupServiceTimeout = 10000;
const grpc_millis kMaxMaxAge = 5 * 60 * GPR_MS_PER_SEC;
const int64_t kDefaultCacheSizeBytes = 10 * 1024 * 1024;
const grpc_millis kMinExpirationTime = 5 * GPR_MS_PER_SEC;
const grpc_millis kCacheBackoffInitial = 1 * GPR_MS_PER_SEC;
const double kCacheBackoffMultiplier = 1.6;
const double kCacheBackoffJitter = 0.2;
const grpc_millis kCacheBackoffMax = 120 * GPR_MS_PER_SEC;
const grpc_millis kDefaultThrottleWindowSize = 30 * GPR_MS_PER_SEC;
const double kDefaultThrottleRatioForSuccesses = 2.0;
const int kDefaultThrottlePaddings = 8;
const grpc_millis kCacheCleanupTimerInterval = 60 * GPR_MS_PER_SEC;

inline const Json* ParseFieldJsonFromJsonObject(const Json::Object& object,
                                                const std::string& field,
                                                grpc_error_handle* error,
                                                bool optional = false) {
  *error = GRPC_ERROR_NONE;
  auto it = object.find(field);
  if (it == object.end()) {
    if (!optional) {
      *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat(field, " field is not found").c_str());
    }
    return nullptr;
  } else {
    return &it->second;
  }
}

const Json::Object* ParseObjectFieldFromJsonObject(const Json::Object& object,
                                                   const std::string& field,
                                                   grpc_error_handle* error,
                                                   bool optional = false) {
  *error = GRPC_ERROR_NONE;
  const Json* child_json =
      ParseFieldJsonFromJsonObject(object, field, error, optional);
  if (*error != GRPC_ERROR_NONE) {
    return nullptr;
  }
  if (child_json == nullptr) return nullptr;
  if (child_json->type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat(field, " field is not an object").c_str());
    return nullptr;
  }
  return &child_json->object_value();
}

const Json::Array* ParseArrayFieldFromJsonObject(const Json::Object& object,
                                                 const std::string& field,
                                                 grpc_error_handle* error,
                                                 bool optional = false) {
  *error = GRPC_ERROR_NONE;
  const Json* child_json =
      ParseFieldJsonFromJsonObject(object, field, error, optional);
  if (*error != GRPC_ERROR_NONE) {
    return nullptr;
  }
  if (child_json == nullptr) return nullptr;
  if (child_json->type() != Json::Type::ARRAY) {
    *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat(field, " field is not an array").c_str());
    return nullptr;
  }
  return &child_json->array_value();
}

const std::string* ParseStringFieldFromJsonObject(const Json::Object& object,
                                                  const std::string& field,
                                                  grpc_error_handle* error,
                                                  bool optional = false) {
  *error = GRPC_ERROR_NONE;
  const Json* child_json =
      ParseFieldJsonFromJsonObject(object, field, error, optional);
  if (*error != GRPC_ERROR_NONE) {
    return nullptr;
  }
  if (child_json == nullptr) return nullptr;
  if (child_json->type() != Json::Type::STRING) {
    *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat(field, " field is not a string").c_str());
    return nullptr;
  }
  return &child_json->string_value();
}

int64_t ParseNumberFieldFromJsonObject(const Json::Object& object,
                                       const std::string& field,
                                       grpc_error_handle* error,
                                       bool optional = false,
                                       int64_t optional_default = 0) {
  *error = GRPC_ERROR_NONE;
  const Json* child_json =
      ParseFieldJsonFromJsonObject(object, field, error, optional);
  if (*error != GRPC_ERROR_NONE) {
    return optional_default;
  }
  if (child_json == nullptr) return optional_default;
  if (child_json->type() != Json::Type::NUMBER) {
    *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("\"", field, "\" field is not a number").c_str());
    return optional_default;
  }
  return strtol(child_json->string_value().c_str(), nullptr, 10);
}

inline grpc_millis ParseDuration(const Json::Object& duration_object,
                                 grpc_error_handle* error) {
  *error = GRPC_ERROR_NONE;
  int64_t seconds =
      ParseNumberFieldFromJsonObject(duration_object, "seconds", error);
  if (*error != GRPC_ERROR_NONE) {
    return 0;
  }
  int32_t nanos =
      ParseNumberFieldFromJsonObject(duration_object, "nanoseconds", error);
  if (*error != GRPC_ERROR_NONE) {
    return 0;
  }
  return seconds * GPR_MS_PER_SEC + nanos / GPR_NS_PER_MS;
}

grpc_error_handle InsertOrUpdateChildPolicyField(Json* config,
                                           const std::string& field,
                                           const std::string& value) {
  if (config->type() != Json::Type::ARRAY) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "child policy configuration is not an array");
  }
  absl::InlinedVector<grpc_error_handle, 1> error_list;
  for (Json& child_json : *config->mutable_array()) {
    if (child_json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "child policy item is not an object"));
    } else {
      Json::Object& child = *child_json.mutable_object();
      if (child.size() != 1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "child policy item contains more than one field"));
      } else {
        Json& child_config_json = child.begin()->second;
        if (child_config_json.type() != Json::Type::OBJECT) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "child policy item config is not an object"));
        } else {
          Json::Object& child_config = *child_config_json.mutable_object();
          child_config[field] = Json(value);
        }
      }
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR(
      absl::StrCat("errors when inserting field \"", field,
                   "\" for child policy")
          .c_str(),
      &error_list);
}

}  //  namespace

const RlsLb::KeyMapBuilder* RlsFindKeyMapBuilder(
    const RlsLb::KeyMapBuilderMap& key_map_builder_map,
    const std::string& path) {
  auto it = key_map_builder_map.find(std::string(path));
  if (it == key_map_builder_map.end()) {
    size_t last_slash_pos = path.rfind("/");
    GPR_DEBUG_ASSERT(last_slash_pos != path.npos);
    if (GPR_UNLIKELY(last_slash_pos == path.npos)) {
      return nullptr;
    }
    std::string service(path, 0, last_slash_pos + 1);
    it = key_map_builder_map.find(service);
    if (it == key_map_builder_map.end()) {
      return nullptr;
    }
  }
  return &(it->second);
}

// Picker implementation
LoadBalancingPolicy::PickResult RlsLb::Picker::Pick(PickArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[rlslb %p] picker=%p: picker pick", lb_policy_.get(),
            this);
  }
  RequestKey key;
  key.path = std::string(args.path);
  std::lock_guard<std::recursive_mutex> lock(lb_policy_->mu_);
  if (lb_policy_->is_shutdown_) {
    return PickResult::Fail(
        absl::UnavailableError("LB policy already shut down"));
  }
  const KeyMapBuilder* key_map_builder =
      lb_policy_->FindKeyMapBuilder(key.path);
  if (key_map_builder != nullptr) {
    key.key_map = key_map_builder->BuildKeyMap(args.initial_metadata);
  }
  Cache::Entry* entry = lb_policy_->cache_.Find(key);
  if (entry == nullptr) {
    bool call_throttled = !lb_policy_->MaybeMakeRlsCall(key);
    if (call_throttled) {
      if (lb_policy_->config_->default_target().empty()) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[rlslb %p] picker=%p: pick failed as the RLS call is "
                  "throttled",
                  lb_policy_.get(), this);
        }
        return PickResult::Fail(
            absl::UnavailableError("RLS request throttled"));
      } else {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[rlslb %p] picker=%p: pick forwarded to default target "
                  "as the RLS call is throttled",
                  lb_policy_.get(), this);
        }
        return lb_policy_->default_child_policy_->child()->Pick(
            std::move(args));
      }
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
        gpr_log(GPR_DEBUG,
                "[rlslb %p] picker=%p: pick queued as the RLS call is made",
                lb_policy_.get(), this);
      }
      return PickResult::Queue();
    }
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
      gpr_log(GPR_DEBUG,
              "[rlslb %p] picker=%p: pick forwarded to cache entry %p",
              lb_policy_.get(), this, entry);
    }
    return entry->Pick(args);
  }
}

// Cache entry implementation

RlsLb::Cache::Entry::Entry(RefCountedPtr<RlsLb> lb_policy)
    : lb_policy_(std::move(lb_policy)),
      min_expiration_time_(ExecCtx::Get()->Now() + kMinExpirationTime) {
  BackOff::Options backoff_options;
  backoff_options.set_initial_backoff(kCacheBackoffInitial)
      .set_multiplier(kCacheBackoffMultiplier)
      .set_jitter(kCacheBackoffJitter)
      .set_max_backoff(kCacheBackoffMax);
  backoff_state_ = std::unique_ptr<BackOff>(new BackOff(backoff_options));
  GRPC_CLOSURE_INIT(&backoff_timer_callback_, OnBackoffTimer, this, nullptr);
}

LoadBalancingPolicy::PickResult RlsLb::Cache::Entry::Pick(PickArgs args) {
  grpc_millis now = ExecCtx::Get()->Now();
  if (stale_time_ < now && backoff_time_ < now) {
    bool call_throttled =
        !lb_policy_->MaybeMakeRlsCall(*lru_iterator_, &backoff_state_);
    if (call_throttled && data_expiration_time_ < now) {
      if (lb_policy_->config_->default_target().empty()) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[rlslb %p] picker=%p: pick failed as the RLS call is "
                  "throttled",
                  lb_policy_.get(), this);
        }
        return PickResult::Fail(
            absl::UnavailableError("RLS request throttled"));
      } else {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[rlslb %p] picker=%p: pick forwarded to default target "
                  "as the RLS call is throttled",
                  lb_policy_.get(), this);
        }
        return lb_policy_->default_child_policy_->child()->Pick(
            std::move(args));
      }
    }
  }
  if (now <= data_expiration_time_) {
    GPR_DEBUG_ASSERT(!child_policy_wrappers_.empty());
    if (child_policy_wrappers_.empty()) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
        gpr_log(GPR_ERROR,
                "[rlslb %p] cache entry=%p: cached response is valid but child "
                "policy wrapper is empty",
                lb_policy_.get(), this);
      }
      return PickResult::Fail(
          absl::UnavailableError("child policy does not exist"));
    } else {
      if (!header_data_.empty()) {
        char* copied_header_data = static_cast<char*>(
            args.call_state->Alloc(header_data_.length() + 1));
        strcpy(copied_header_data, header_data_.c_str());
        args.initial_metadata->Add(kRlsHeaderKey, copied_header_data);
      }
      for (RefCountedPtr<ChildPolicyOwner>& child_policy_wrapper :
           child_policy_wrappers_) {
        switch (child_policy_wrapper->child()->ConnectivityState()) {
          case GRPC_CHANNEL_READY:
            if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
              gpr_log(GPR_DEBUG,
                      "[rlslb %p] cache entry=%p: pick forwarded to child "
                      "policy %p",
                      lb_policy_.get(), this, child_policy_wrapper->child());
            }
            return child_policy_wrapper->child()->Pick(args);
          case GRPC_CHANNEL_IDLE:
            if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
              gpr_log(GPR_DEBUG,
                      "[rlslb %p] cache entry=%p: pick forwarded to IDLE state "
                      "child policy %p",
                      lb_policy_.get(), this, child_policy_wrapper->child());
            }
            return child_policy_wrapper->child()->Pick(args);
          case GRPC_CHANNEL_CONNECTING:
            gpr_log(GPR_DEBUG,
                    "[rlslb %p] cache entry=%p: pick queued due to child "
                    "policy in CONNECTING state",
                    lb_policy_.get(), this);
            return PickResult::Queue();
          default:
            continue;
        }
      }
      return PickResult::Fail(absl::UnavailableError("RLS request in backoff"));
    }
  } else if (now <= backoff_time_) {
    if (lb_policy_->config_->default_target().empty()) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
        gpr_log(GPR_DEBUG,
                "[rlslb %p] cache entry=%p: pick failed due to backoff",
                lb_policy_.get(), this);
      }
      return PickResult::Fail(absl::UnavailableError("RLS request in backoff"));
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
        gpr_log(GPR_DEBUG,
                "[rlslb %p] cache entry=%p: pick forwarded to the default "
                "child policy",
                lb_policy_.get(), this);
      }
      return lb_policy_->default_child_policy_->child()->Pick(args);
    }
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
      gpr_log(GPR_DEBUG,
              "[rlslb %p] cache entry=%p: pick queued and started "
              "refreshing request",
              lb_policy_.get(), this);
    }
    return PickResult::Queue();
  }
}

void RlsLb::Cache::Entry::ResetBackoff() {
  backoff_time_ = GRPC_MILLIS_INF_PAST;
  if (timer_pending_) {
    grpc_timer_cancel(&backoff_timer_);
    timer_pending_ = false;
  }
}

bool RlsLb::Cache::Entry::ShouldRemove() const {
  grpc_millis now = ExecCtx::Get()->Now();
  return (data_expiration_time_ < now && backoff_expiration_time_ < now);
}

bool RlsLb::Cache::Entry::CanEvict() const {
  grpc_millis now = ExecCtx::Get()->Now();
  return (min_expiration_time_ < now);
}

void RlsLb::Cache::Entry::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[rlslb %p] cache entry=%p: cache entry evicted",
            lb_policy_.get(), this);
  }
  is_shutdown_ = true;
  if (status_ != GRPC_ERROR_NONE) {
    GRPC_ERROR_UNREF(status_);
    status_ = GRPC_ERROR_NONE;
  }
  backoff_state_.reset();
  if (timer_pending_) {
    grpc_timer_cancel(&backoff_timer_);
    lb_policy_->UpdatePicker();
  }
  child_policy_wrappers_.clear();
}

void RlsLb::Cache::Entry::OnRlsResponseLocked(
    ResponseInfo response, std::unique_ptr<BackOff> backoff_state) {
  if (response.error == GRPC_ERROR_NONE) {
    bool same_targets = true;
    if (child_policy_wrappers_.size() != response.targets.size()) {
      same_targets = false;
    } else {
      int i = 0;
      for (std::string& target : response.targets) {
        if (target != child_policy_wrappers_[i]->child()->target()) {
          same_targets = false;
          break;
        }
        i++;
      }
    }
    if (same_targets) {
      lb_policy_->UpdatePicker();
    } else {
      std::set<absl::string_view> old_targets;
      for (RefCountedPtr<ChildPolicyOwner>& child_policy_wrapper :
           child_policy_wrappers_) {
        old_targets.emplace(child_policy_wrapper->child()->target());
      }
      bool update_picker = false;
      std::vector<RefCountedPtr<ChildPolicyOwner>> new_child_policy_wrappers;
      new_child_policy_wrappers.reserve(response.targets.size());
      for (std::string& target : response.targets) {
        auto it = lb_policy_->child_policy_map_.find(target);
        if (it == lb_policy_->child_policy_map_.end()) {
          new_child_policy_wrappers.emplace_back(
              MakeRefCounted<ChildPolicyOwner>(
                  MakeOrphanable<ChildPolicyWrapper>(lb_policy_->Ref(), target),
                  lb_policy_.get()));
          Json copied_child_policy_config =
              lb_policy_->config_->child_policy_config();
          grpc_error_handle error = InsertOrUpdateChildPolicyField(
              &copied_child_policy_config,
              lb_policy_->config_->child_policy_config_target_field_name(),
              target);
          GPR_ASSERT(error == GRPC_ERROR_NONE);
          new_child_policy_wrappers.back()->child()->UpdateLocked(
              copied_child_policy_config, lb_policy_->addresses_,
              lb_policy_->channel_args_);
        } else {
          new_child_policy_wrappers.emplace_back(it->second->Ref());
          if (old_targets.find(target) == old_targets.end()) {
            update_picker = true;
          }
        }
      }
      child_policy_wrappers_ = std::move(new_child_policy_wrappers);
      if (update_picker) {
        lb_policy_->UpdatePicker();
      }
    }
    header_data_ = std::move(response.header_data);
    grpc_millis now = ExecCtx::Get()->Now();
    data_expiration_time_ = now + lb_policy_->config_->max_age();
    stale_time_ = now + lb_policy_->config_->stale_age();
    status_ = GRPC_ERROR_NONE;
    backoff_state_.reset();
    backoff_time_ = GRPC_MILLIS_INF_PAST;
    backoff_expiration_time_ = GRPC_MILLIS_INF_PAST;
  } else {
    status_ = GRPC_ERROR_REF(response.error);
    if (backoff_state != nullptr) {
      backoff_state_ = std::move(backoff_state);
    } else {
      BackOff::Options backoff_options;
      backoff_options.set_initial_backoff(kCacheBackoffInitial)
          .set_multiplier(kCacheBackoffMultiplier)
          .set_jitter(kCacheBackoffJitter)
          .set_max_backoff(kCacheBackoffMax);
      backoff_state_ = std::unique_ptr<BackOff>(new BackOff(backoff_options));
    }
    backoff_time_ = backoff_state_->NextAttemptTime();
    grpc_millis now = ExecCtx::Get()->Now();
    backoff_expiration_time_ = now + (backoff_time_ - now) * 2;
    if (lb_policy_->config_->default_target().empty()) {
      timer_pending_ = true;
      Ref().release();
      grpc_timer_init(&backoff_timer_, backoff_time_, &backoff_timer_callback_);
    }
    lb_policy_->UpdatePicker();
  }
  // move the entry to the end of the LRU list
  Cache& cache = lb_policy_->cache_;
  cache.lru_list_.push_back(*lru_iterator_);
  cache.lru_list_.erase(lru_iterator_);
  lru_iterator_ = cache.lru_list_.end();
  lru_iterator_--;
}

void RlsLb::Cache::Entry::OnBackoffTimer(void* arg, grpc_error_handle error) {
  RefCountedPtr<Entry> entry(static_cast<Entry*>(arg));
  entry->lb_policy_->work_serializer()->Run(
      [&entry, error]() {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[rlslb %p] cache entry=%p, error=%p: successful RLS "
                  "response received",
                  entry->lb_policy_.get(), entry.get(), error);
        }
        entry->timer_pending_ = false;
        // The pick was in backoff state and there could be a pick queued if
        // wait_for_ready is true. We'll update the picker for that case.
        entry->lb_policy_->UpdatePicker();
      },
      DEBUG_LOCATION);
}

RlsLb::Cache::Cache(RlsLb* lb_policy) : lb_policy_(lb_policy) {
  grpc_millis now = ExecCtx::Get()->Now();
  lb_policy_->Ref().release();
  GRPC_CLOSURE_INIT(&timer_callback_, OnCleanupTimer, this, nullptr);
  grpc_timer_init(&cleanup_timer_, now + kCacheCleanupTimerInterval,
                  &timer_callback_);
}

RlsLb::Cache::Entry* RlsLb::Cache::Find(const RequestKey& key) {
  auto it = map_.find(key);
  if (it == map_.end()) {
    return nullptr;
  } else {
    SetRecentUsage(it);
    return it->second.get();
  }
}

RlsLb::Cache::Entry* RlsLb::Cache::FindOrInsert(const RequestKey& key) {
  auto it = map_.find(key);
  if (it == map_.end()) {
    size_t new_entry_size = key.Size() * 2 + sizeof(Entry);
    MaybeShrinkSize(size_limit_ - new_entry_size);
    Entry* entry = new Entry(lb_policy_->Ref());
    map_.emplace(key, OrphanablePtr<Entry>(entry));
    auto lru_it = lru_list_.insert(lru_list_.end(), key);
    entry->set_iterator(lru_it);
    size_ += new_entry_size;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
      gpr_log(GPR_DEBUG, "[rlslb %p] cache entry added, entry=%p", lb_policy_,
              entry);
    }
    return entry;
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
      gpr_log(GPR_DEBUG, "[rlslb %p] cache entry found, entry=%p", lb_policy_,
              it->second.get());
    }
    SetRecentUsage(it);
    return it->second.get();
  }
}

void RlsLb::Cache::Resize(int64_t bytes) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[rlslb %p] bytes=%" PRId64 ": cache resized",
            lb_policy_, bytes);
  }
  size_limit_ = bytes;
  MaybeShrinkSize(size_limit_);
}

void RlsLb::Cache::ResetAllBackoff() {
  for (auto& e : map_) {
    e.second->ResetBackoff();
  }
}

void RlsLb::Cache::Shutdown() {
  map_.clear();
  lru_list_.clear();
  grpc_timer_cancel(&cleanup_timer_);
}

void RlsLb::Cache::OnCleanupTimer(void* arg, grpc_error_handle error) {
  Cache* cache = static_cast<Cache*>(arg);
  cache->lb_policy_->work_serializer()->Run(
      [&cache, error]() {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[rlslb %p] cache=%p, error=%p: cleanup timer fired",
                  cache->lb_policy_, cache, error);
        }
        if (error == GRPC_ERROR_CANCELLED || cache->lb_policy_->is_shutdown_)
          return;
        std::lock_guard<std::recursive_mutex> lock(cache->lb_policy_->mu_);
        for (auto it = cache->map_.begin(); it != cache->map_.end();) {
          if (GPR_UNLIKELY(it->second->ShouldRemove())) {
            if (!it->second->CanEvict()) break;
            auto lru_it = it->second->iterator();
            size_t key_size = lru_it->Size();
            cache->size_ -= (key_size   /* entry in lru_list_ */
                             + key_size /* key of entry in map_ */
                             + sizeof(Entry) /* value of entry in map_ */);
            it = cache->map_.erase(it);
            cache->lru_list_.erase(lru_it);
          } else {
            it++;
          }
        }
        grpc_millis now = ExecCtx::Get()->Now();
        cache->lb_policy_->Ref().release();
        grpc_timer_init(&cache->cleanup_timer_,
                        now + kCacheCleanupTimerInterval,
                        &cache->timer_callback_);
      },
      DEBUG_LOCATION);
  cache->lb_policy_->Unref();
}

void RlsLb::Cache::MaybeShrinkSize(int64_t bytes) {
  while (size_ > bytes) {
    auto lru_it = lru_list_.begin();
    if (GPR_UNLIKELY(lru_it == lru_list_.end())) break;
    auto map_it = map_.find(*lru_it);
    GPR_ASSERT(map_it != map_.end());
    if (!map_it->second->CanEvict()) break;
    size_t key_size = lru_it->Size();
    size_ -= (key_size   /* entry in lru_list_ */
              + key_size /* key of entry in map_ */
              + sizeof(Entry) /* value of entry in map_ */);
    map_.erase(map_it);
    lru_list_.erase(lru_it);
  }
}

void RlsLb::Cache::SetRecentUsage(MapType::iterator entry) {
  auto lru_it = entry->second->iterator();
  auto new_it = lru_list_.insert(lru_list_.end(), *lru_it);
  lru_list_.erase(lru_it);
  entry->second->set_iterator(new_it);
}

// Request map implementation

RlsLb::RlsRequest::RlsRequest(RefCountedPtr<RlsLb> lb_policy, RequestKey key,
                              RefCountedPtr<ControlChannel> channel,
                              std::unique_ptr<BackOff> backoff_state)
    : lb_policy_(std::move(lb_policy)),
      key_(std::move(key)),
      channel_(std::move(channel)),
      backoff_state_(std::move(backoff_state)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[rlslb %p] request_map_entry=%p: request map entry created",
            lb_policy_.get(), this);
  }
  GRPC_CLOSURE_INIT(&call_complete_cb_, OnRlsCallComplete, this, nullptr);
  ExecCtx::Run(
      DEBUG_LOCATION,
      GRPC_CLOSURE_INIT(&call_start_cb_, StartCall, Ref().release(), nullptr),
      GRPC_ERROR_NONE);
}

RlsLb::RlsRequest::~RlsRequest() {
  if (call_ != nullptr) {
    GRPC_CALL_INTERNAL_UNREF(call_, "request map destroyed");
  }
  grpc_byte_buffer_destroy(send_message_);
  grpc_byte_buffer_destroy(recv_message_);
  grpc_metadata_array_destroy(&recv_initial_metadata_);
  grpc_metadata_array_destroy(&recv_trailing_metadata_);
  grpc_slice_unref_internal(status_details_recv_);
}

void RlsLb::RlsRequest::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[rlslb %p] request_map_entry=%p: request map entry shutdown",
            lb_policy_.get(), this);
  }
  if (call_ != nullptr) {
    grpc_call_cancel_internal(call_);
    call_ = nullptr;
  }
}

void RlsLb::RlsRequest::StartCall(void* arg, grpc_error_handle error) {
  (void)error;
  RefCountedPtr<RlsRequest> entry(reinterpret_cast<RlsRequest*>(arg));
  grpc_millis now = ExecCtx::Get()->Now();
  grpc_call* call = grpc_channel_create_pollset_set_call(
      entry->channel_->channel(), nullptr, GRPC_PROPAGATE_DEFAULTS,
      entry->lb_policy_->interested_parties(),
      grpc_slice_from_static_string(kRlsRequestPath), nullptr,
      now + entry->lb_policy_->config_->lookup_service_timeout(), nullptr);
  grpc_op ops[6];
  memset(ops, 0, sizeof(ops));
  grpc_op* op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY |
              GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  entry->send_message_ = entry->MakeRequestProto();
  op->data.send_message.send_message = entry->send_message_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &entry->recv_initial_metadata_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &entry->recv_message_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &entry->recv_trailing_metadata_;
  op->data.recv_status_on_client.status = &entry->status_recv_;
  op->data.recv_status_on_client.status_details = &entry->status_details_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  entry->Ref().release();
  auto call_error = grpc_call_start_batch_and_execute(
      call, ops, static_cast<size_t>(op - ops), &entry->call_complete_cb_);
  GPR_ASSERT(call_error == GRPC_CALL_OK);
  std::lock_guard<std::recursive_mutex> lock(entry->lb_policy_->mu_);
  if (entry->lb_policy_->is_shutdown_) {
    grpc_call_cancel_internal(call);
  } else {
    entry->call_ = call;
  }
}

void RlsLb::RlsRequest::OnRlsCallComplete(void* arg, grpc_error_handle error) {
  RefCountedPtr<RlsRequest> entry(static_cast<RlsRequest*>(arg));
  entry->lb_policy_->work_serializer()->Run(
      [entry, error]() { entry->OnRlsCallCompleteLocked(error); },
      DEBUG_LOCATION);
}

void RlsLb::RlsRequest::OnRlsCallCompleteLocked(grpc_error_handle error) {
  if (lb_policy_->is_shutdown_) return;
  bool call_failed =
      (error != GRPC_ERROR_NONE || status_recv_ != GRPC_STATUS_OK);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[rlslb %p] request_map_entry=%p, error=%p, status=%d: RLS call "
            "response received",
            lb_policy_.get(), this, error, status_recv_);
  }
  ResponseInfo res;
  if (call_failed) {
    if (error == GRPC_ERROR_NONE) {
      res.error = grpc_error_set_str(
          grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("received error status"),
              GRPC_ERROR_INT_GRPC_STATUS, status_recv_),
          GRPC_ERROR_STR_GRPC_MESSAGE, status_details_recv_);
    } else {
      res.error = error;
    }
  } else {
    res = ParseResponseProto();
  }
  std::lock_guard<std::recursive_mutex> lock(lb_policy_->mu_);
  channel_->ReportResponseLocked(call_failed);
  Cache::Entry* cache_entry = lb_policy_->cache_.FindOrInsert(key_);
  cache_entry->OnRlsResponseLocked(std::move(res), std::move(backoff_state_));
  lb_policy_->request_map_.erase(key_);
}

grpc_byte_buffer* RlsLb::RlsRequest::MakeRequestProto() {
  upb::Arena arena;
  grpc_lookup_v1_RouteLookupRequest* req =
      grpc_lookup_v1_RouteLookupRequest_new(arena.ptr());
  grpc_lookup_v1_RouteLookupRequest_set_server(
      req, upb_strview_make(lb_policy_->server_name_.c_str(),
                            lb_policy_->server_name_.length()));
  grpc_lookup_v1_RouteLookupRequest_set_path(
      req, upb_strview_make(key_.path.c_str(), key_.path.length()));
  grpc_lookup_v1_RouteLookupRequest_set_target_type(
      req, upb_strview_make(kGrpc, sizeof(kGrpc)));
  for (auto& kv : key_.key_map) {
    grpc_lookup_v1_RouteLookupRequest_key_map_set(
        req, upb_strview_make(kv.first.c_str(), kv.first.length()),
        upb_strview_make(kv.second.c_str(), kv.second.length()), arena.ptr());
  }
  size_t len;
  char* buf =
      grpc_lookup_v1_RouteLookupRequest_serialize(req, arena.ptr(), &len);
  grpc_slice send_slice = grpc_slice_from_copied_buffer(buf, len);
  return grpc_raw_byte_buffer_create(&send_slice, 1);
}

RlsLb::ResponseInfo RlsLb::RlsRequest::ParseResponseProto() {
  ResponseInfo result;
  result.error = GRPC_ERROR_NONE;
  upb::Arena arena;
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, recv_message_);
  grpc_slice recv_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_lookup_v1_RouteLookupResponse* res =
      grpc_lookup_v1_RouteLookupResponse_parse(
          reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(recv_slice)),
          GRPC_SLICE_LENGTH(recv_slice), arena.ptr());
  if (res == nullptr) {
    result.error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("cannot parse RLS response");
    return result;
  }
  size_t len;
  const upb_strview* targets_strview =
      grpc_lookup_v1_RouteLookupResponse_targets(res, &len);
  if (len == 0) {
    result.error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "RLS response has no target entry");
    return result;
  }
  result.targets.reserve(len);
  for (int i = 0; i < len; i++) {
    result.targets.emplace_back(targets_strview[i].data,
                                targets_strview[i].size);
  }
  upb_strview header_data_strview =
      grpc_lookup_v1_RouteLookupResponse_header_data(res);
  result.header_data =
      std::string(header_data_strview.data, header_data_strview.size);
  grpc_slice_unref_internal(recv_slice);
  return result;
}

// ControlChannel implementation

RlsLb::ControlChannel::ControlChannel(RefCountedPtr<RlsLb> lb_policy,
                                      const std::string& target,
                                      const grpc_channel_args* channel_args)
    : lb_policy_(std::move(lb_policy)) {
  grpc_channel_credentials* creds =
      grpc_channel_credentials_find_in_args(channel_args);
  channel_ =
      grpc_secure_channel_create(creds, target.c_str(), nullptr, nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[rlslb %p] ControlChannel=%p, channel=%p: control channel created",
            lb_policy.get(), this, channel_);
  }
  if (channel_ != nullptr) {
    ClientChannel* client_channel = ClientChannel::GetFromChannel(channel_);
    GPR_ASSERT(client_channel != nullptr);
    watcher_ = new StateWatcher(Ref(DEBUG_LOCATION, "StateWatcher"));
    client_channel->AddConnectivityWatcher(
        GRPC_CHANNEL_IDLE,
        OrphanablePtr<AsyncConnectivityStateWatcherInterface>(watcher_));
  }
}

void RlsLb::ControlChannel::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(
        GPR_DEBUG,
        "[rlslb %p] ControlChannel=%p, channel=%p: control channel shutdown",
        lb_policy_.get(), this, channel_);
  }
  is_shutdown_ = true;
  if (channel_ != nullptr) {
    if (watcher_ != nullptr) {
      ClientChannel* client_channel = ClientChannel::GetFromChannel(channel_);
      GPR_ASSERT(client_channel != nullptr);
      client_channel->RemoveConnectivityWatcher(watcher_);
      watcher_ = nullptr;
    }
    grpc_channel_destroy(channel_);
  }
}

void RlsLb::ControlChannel::ReportResponseLocked(bool response_succeeded) {
  throttle_.RegisterResponse(response_succeeded);
}

void RlsLb::ControlChannel::ResetBackoff() {
  GPR_DEBUG_ASSERT(channel_ != nullptr);
  grpc_channel_reset_connect_backoff(channel_);
}

// Throttle implementation
RlsLb::ControlChannel::Throttle::Throttle(int window_size_seconds,
                                          double ratio_for_successes,
                                          int paddings) {
  GPR_DEBUG_ASSERT(window_size_seconds >= 0);
  GPR_DEBUG_ASSERT(ratio_for_successes >= 0);
  GPR_DEBUG_ASSERT(paddings >= 0);
  window_size_ = window_size_seconds == 0 ? window_size_seconds * GPR_MS_PER_SEC
                                          : kDefaultThrottleWindowSize;
  ratio_for_successes_ = ratio_for_successes == 0
                             ? kDefaultThrottleRatioForSuccesses
                             : ratio_for_successes;
  paddings_ = paddings == 0 ? kDefaultThrottlePaddings : paddings;
}

bool RlsLb::ControlChannel::Throttle::ShouldThrottle() {
  grpc_millis now = ExecCtx::Get()->Now();
  while (requests_.size() > 0 && now - requests_.front() > window_size_) {
    requests_.pop_front();
  }
  while (successes_.size() > 0 && now - successes_.front() > window_size_) {
    successes_.pop_front();
  }
  int successes = successes_.size();
  int requests = requests_.size();
  bool result = ((rand() % (requests + paddings_)) <
                 static_cast<double>(requests) -
                     static_cast<double>(successes) * ratio_for_successes_);
  requests_.push_back(now);
  return result;
}

void RlsLb::ControlChannel::Throttle::RegisterResponse(bool success) {
  if (success) {
    successes_.push_back(ExecCtx::Get()->Now());
  }
}

// StateWatcher implementation
RlsLb::ControlChannel::StateWatcher::StateWatcher(
    RefCountedPtr<ControlChannel> channel)
    : AsyncConnectivityStateWatcherInterface(
          channel->lb_policy_->work_serializer()),
      channel_(std::move(channel)) {}

void RlsLb::ControlChannel::StateWatcher::OnConnectivityStateChange(
    grpc_connectivity_state new_state, const absl::Status& status) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[rlslb %p] ControlChannel=%p StateWatcher=%p: "
            "state changed to %s (%s)",
            channel_->lb_policy_.get(), channel_.get(), this,
            ConnectivityStateName(new_state), status.ToString().c_str());
  }
  std::lock_guard<std::recursive_mutex> lock(channel_->lb_policy_->mu_);
  if (channel_->is_shutdown_) return;
  if (new_state == GRPC_CHANNEL_READY && was_transient_failure_) {
    was_transient_failure_ = false;
    channel_->lb_policy_->cache_.ResetAllBackoff();
    if (channel_->lb_policy_->config_->default_target().empty()) {
      channel_->lb_policy_->UpdatePicker();
    }
  } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    was_transient_failure_ = true;
  }
}

RlsLb::ChildPolicyOwner::ChildPolicyOwner(
    OrphanablePtr<ChildPolicyWrapper> child, RlsLb* parent)
    : parent_(parent), child_(std::move(child)) {
  parent_->child_policy_map_.emplace(child_->target(), this);
}

RlsLb::ChildPolicyOwner::~ChildPolicyOwner() {
  parent_->child_policy_map_.erase(child_->target());
}

// ChildPolicyWrapper implementation
LoadBalancingPolicy::PickResult RlsLb::ChildPolicyWrapper::Pick(PickArgs args) {
  if (picker_ == nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
      gpr_log(GPR_DEBUG,
              "[rlslb %p] ChildPolicyWrapper=%p: pick queued as the picker "
              "is not ready",
              lb_policy_.get(), this);
    }
    return PickResult::Queue();
  } else {
    return picker_->Pick(args);
  }
}

void RlsLb::ChildPolicyWrapper::UpdateLocked(
    const Json& child_policy_config, ServerAddressList addresses,
    const grpc_channel_args* channel_args) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  UpdateArgs update_args;
  update_args.config = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
      child_policy_config, &error);
  GPR_DEBUG_ASSERT(error == GRPC_ERROR_NONE);
  // returned RLS target fails the validation
  if (error != GRPC_ERROR_NONE) {
    picker_ = std::unique_ptr<LoadBalancingPolicy::SubchannelPicker>(
        new TransientFailurePicker(grpc_error_to_absl_status(error)));
    GRPC_ERROR_UNREF(error);
    child_policy_ = nullptr;
    return;
  }
  Args create_args;
  create_args.work_serializer = lb_policy_->work_serializer();
  create_args.channel_control_helper =
      absl::make_unique<ChildPolicyHelper>(Ref());
  create_args.args = channel_args;
  if (child_policy_ == nullptr) {
    child_policy_ = MakeOrphanable<ChildPolicyHandler>(std::move(create_args),
                                                       &grpc_lb_rls_trace);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
      gpr_log(GPR_INFO,
              "[rlslb %p] ChildPolicyWrapper=%p, create new child policy "
              "handler %p",
              lb_policy_.get(), this, child_policy_.get());
    }
    grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                     lb_policy_->interested_parties());
  }
  update_args.addresses = std::move(addresses);
  update_args.args = grpc_channel_args_copy(channel_args);
  child_policy_->UpdateLocked(std::move(update_args));
}

void RlsLb::ChildPolicyWrapper::ExitIdleLocked() {
  if (child_policy_ != nullptr) {
    child_policy_->ExitIdleLocked();
  }
}

void RlsLb::ChildPolicyWrapper::ResetBackoffLocked() {
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
  }
}

grpc_connectivity_state RlsLb::ChildPolicyWrapper::ConnectivityState() const {
  if (was_transient_failure_) {
    return GRPC_CHANNEL_TRANSIENT_FAILURE;
  } else {
    return connectivity_state_;
  }
}

void RlsLb::ChildPolicyWrapper::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[rlslb %p] ChildPolicyWrapper=%p: child policy wrapper shutdown",
            lb_policy_.get(), this);
  }
  is_shutdown_ = true;
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     lb_policy_->interested_parties());
    child_policy_.reset();
  }
  picker_.reset();
}

// ChildPolicyHelper implementation
RefCountedPtr<SubchannelInterface>
RlsLb::ChildPolicyWrapper::ChildPolicyHelper::CreateSubchannel(
    ServerAddress address, const grpc_channel_args& args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[rlslb %p] ChildPolicyHelper=%p ChildPolicyWrapper=%p: "
            "CreateSubchannel() for %s",
            wrapper_->lb_policy_.get(), this, wrapper_.get(),
            address.ToString().c_str());
  }
  if (wrapper_->is_shutdown_) return nullptr;
  return wrapper_->lb_policy_->channel_control_helper()->CreateSubchannel(
      std::move(address), args);
}

void RlsLb::ChildPolicyWrapper::ChildPolicyHelper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    std::unique_ptr<SubchannelPicker> picker) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[rlslb %p] ChildPolicyHelper=%p ChildPolicyWrapper=%p: "
            "UpdateState(state=%s, status=%s, picker=%p)",
            wrapper_->lb_policy_.get(), this, wrapper_.get(),
            ConnectivityStateName(state), status.ToString().c_str(),
            picker.get());
  }
  std::lock_guard<std::recursive_mutex> lock(wrapper_->lb_policy_->mu_);
  if (wrapper_->is_shutdown_) return;
  wrapper_->connectivity_state_ = state;
  if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    wrapper_->was_transient_failure_ = true;
  } else if (state == GRPC_CHANNEL_READY) {
    wrapper_->was_transient_failure_ = false;
  }
  GPR_DEBUG_ASSERT(picker != nullptr);
  if (picker != nullptr) {
    wrapper_->picker_ = std::move(picker);
  }
  wrapper_->lb_policy_->UpdatePicker();
}

void RlsLb::ChildPolicyWrapper::ChildPolicyHelper::RequestReresolution() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[rlslb %p] ChildPolicyHelper=%p, ChildPolicyWrapper=%p: "
            "RequestReresolution",
            wrapper_->lb_policy_.get(), this, wrapper_.get());
  }
  if (wrapper_->is_shutdown_) return;
  wrapper_->lb_policy_->channel_control_helper()->RequestReresolution();
}

void RlsLb::ChildPolicyWrapper::ChildPolicyHelper::AddTraceEvent(
    TraceSeverity severity, absl::string_view message) {
  if (wrapper_->is_shutdown_) return;
  wrapper_->lb_policy_->channel_control_helper()->AddTraceEvent(severity,
                                                                message);
}

// RlsLb implementation

RlsLb::RlsLb(Args args) : LoadBalancingPolicy(std::move(args)), cache_(this) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[rlslb %p] policy created", this);
  }
}

const char* RlsLb::name() const { return kRls; }

void RlsLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[rlslb %p] policy updated", this);
  }
  ServerAddressList old_addresses = std::move(addresses_);
  addresses_ = args.addresses;
  grpc_channel_args_destroy(channel_args_);
  channel_args_ = grpc_channel_args_copy(args.args);
  const grpc_arg* arg = grpc_channel_args_find(args.args, GRPC_ARG_SERVER_URI);
  const char* server_uri_str = grpc_channel_arg_get_string(arg);
  GPR_ASSERT(server_uri_str != nullptr);
  absl::StatusOr<URI> uri = URI::Parse(server_uri_str);
  GPR_ASSERT(uri.ok());
  server_name_ = std::string(absl::StripPrefix(uri->path(), "/"));
  mu_.lock();
  RefCountedPtr<RlsLbConfig> old_config = config_;
  config_ = args.config;
  if (old_config == nullptr ||
      config_->lookup_service() != old_config->lookup_service()) {
    channel_ = RefCountedPtr<ControlChannel>(
        new ControlChannel(Ref(), config_->lookup_service(), channel_args_));
  }
  if (old_config == nullptr ||
      config_->cache_size_bytes() != old_config->cache_size_bytes()) {
    if (config_->cache_size_bytes() != 0) {
      cache_.Resize(config_->cache_size_bytes());
    } else {
      cache_.Resize(kDefaultCacheSizeBytes);
    }
  }
  if (old_config == nullptr ||
      config_->default_target() != old_config->default_target()) {
    if (config_->default_target().empty()) {
      default_child_policy_.reset();
    } else {
      auto it = child_policy_map_.find(config_->default_target());
      if (it == child_policy_map_.end()) {
        default_child_policy_ = MakeRefCounted<ChildPolicyOwner>(
            MakeOrphanable<ChildPolicyWrapper>(Ref(),
                                               config_->default_target()),
            this);
        default_child_policy_->child()->UpdateLocked(
            config_->child_policy_config(), addresses_, channel_args_);
      } else {
        default_child_policy_ = it->second->Ref();
      }
    }
  }
  if (old_config == nullptr ||
      (config_->child_policy_config() != old_config->child_policy_config()) ||
      (addresses_ != old_addresses)) {
    for (auto& child : child_policy_map_) {
      Json copied_child_policy_config = config_->child_policy_config();
      grpc_error_handle error = InsertOrUpdateChildPolicyField(
          &copied_child_policy_config,
          config_->child_policy_config_target_field_name(),
          child.second->child()->target());
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      child.second->child()->UpdateLocked(copied_child_policy_config,
                                          addresses_, channel_args_);
    }
    if (default_child_policy_ != nullptr) {
      Json copied_child_policy_config = config_->child_policy_config();
      grpc_error_handle error = InsertOrUpdateChildPolicyField(
          &copied_child_policy_config,
          config_->child_policy_config_target_field_name(),
          default_child_policy_->child()->target());
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      default_child_policy_->child()->UpdateLocked(copied_child_policy_config,
                                                   addresses_, channel_args_);
    }
  }
  mu_.unlock();
  UpdatePicker();
}

void RlsLb::ExitIdleLocked() {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  for (auto& child_entry : child_policy_map_) {
    child_entry.second->child()->ExitIdleLocked();
  }
}

void RlsLb::ResetBackoffLocked() {
  mu_.lock();
  channel_->ResetBackoff();
  cache_.ResetAllBackoff();
  mu_.unlock();
  for (auto& child : child_policy_map_) {
    child.second->child()->ResetBackoffLocked();
  }
}

void RlsLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[rlslb %p] policy shutdown", this);
  }
  std::lock_guard<std::recursive_mutex> lock(mu_);
  is_shutdown_ = true;
  config_.reset();
  if (channel_args_ != nullptr) {
    grpc_channel_args_destroy(channel_args_);
  }
  cache_.Shutdown();
  request_map_.clear();
  channel_.reset();
  default_child_policy_.reset();
}

const RlsLb::KeyMapBuilder* RlsLb::FindKeyMapBuilder(
    const std::string& path) const {
  return RlsFindKeyMapBuilder(config_->key_map_builder_map(), path);
}

bool RlsLb::MaybeMakeRlsCall(const RequestKey& key,
                             std::unique_ptr<BackOff>* backoff_state) {
  auto it = request_map_.find(key);
  if (it == request_map_.end()) {
    if (channel_->ShouldThrottle()) {
      return false;
    }
    request_map_.emplace(
        key,
        MakeOrphanable<RlsRequest>(
            Ref(), key, channel_,
            backoff_state == nullptr ? nullptr : std::move(*backoff_state)));
  }
  return true;
}

void RlsLb::UpdatePicker() {
  Ref().release();
  ExecCtx::Run(DEBUG_LOCATION,
               GRPC_CLOSURE_CREATE(UpdatePickerCallback, this,
                                   grpc_schedule_on_exec_ctx),
               GRPC_ERROR_NONE);
}

void RlsLb::UpdatePickerCallback(void* arg, grpc_error_handle error) {
  RefCountedPtr<RlsLb> lb_policy(static_cast<RlsLb*>(arg));
  lb_policy->work_serializer()->Run(
      [lb_policy]() {
        if (lb_policy->is_shutdown_) return;
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG, "[rlslb %p] update picker", lb_policy.get());
        }
        grpc_connectivity_state state = GRPC_CHANNEL_TRANSIENT_FAILURE;
        int num_idle = 0;
        int num_connecting = 0;
        lb_policy->mu_.lock();
        for (auto& item : lb_policy->child_policy_map_) {
          grpc_connectivity_state item_state =
              item.second->child()->ConnectivityState();
          if (item_state == GRPC_CHANNEL_READY) {
            state = GRPC_CHANNEL_READY;
            break;
          } else if (item_state == GRPC_CHANNEL_CONNECTING) {
            num_connecting++;
          } else if (item_state == GRPC_CHANNEL_IDLE) {
            num_idle++;
          }
        }
        if (state != GRPC_CHANNEL_READY) {
          if (num_connecting > 0) {
            state = GRPC_CHANNEL_CONNECTING;
          } else if (num_idle > 0) {
            state = GRPC_CHANNEL_IDLE;
          }
        }
        lb_policy->mu_.unlock();
        absl::Status status;
        if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
          status = absl::UnavailableError("no children available");
        }
        lb_policy->channel_control_helper()->UpdateState(
            state, status, absl::make_unique<Picker>(lb_policy->Ref()));
      },
      DEBUG_LOCATION);
}

RlsLb::KeyMap RlsLb::KeyMapBuilder::BuildKeyMap(
    const MetadataInterface* initial_metadata) const {
  if (initial_metadata == nullptr) return KeyMap();
  KeyMap key_map;
  for (const auto& p : pattern_) {
    const std::string& key = p.first;
    const std::vector<std::string>& names = p.second;
    for (const auto& name : names) {
      std::string buffer;
      absl::optional<absl::string_view> value =
          initial_metadata->Lookup(name, &buffer);
      if (value.has_value()) {
        key_map[key] = std::string(*value);
        break;
      }
    }
  }
  return key_map;
}

const char* RlsLbConfig::name() const { return kRls; }

const char* RlsLbFactory::name() const { return kRls; }

OrphanablePtr<LoadBalancingPolicy> RlsLbFactory::CreateLoadBalancingPolicy(
    LoadBalancingPolicy::Args args) const {
  return MakeOrphanable<RlsLb>(std::move(args));
}

namespace {



      // Fill in the child_policy_config_target_field_name field with default
      // target for all the child policy config and validate them
      Json new_child_policy_array_json = *child_policy_array_json_ptr;
      Json::Array* new_child_policy_array =
          new_child_policy_array_json.mutable_array();
      for (Json& child_policy_json : *new_child_policy_array) {
        if (child_policy_json.type() != Json::Type::OBJECT) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "array element of \"childPolicy\" is not object"));
          continue;
        } else if (child_policy_json.object_value().empty()) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "no policy found in child entry"));
          continue;
        } else if (child_policy_json.object_value().size() > 1) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "oneOf violation in child entry"));
          continue;
        } else {
          Json::Object* child_policy = child_policy_json.mutable_object();
          auto it = child_policy->begin();
          Json& child_policy_config_json = it->second;
          if (child_policy_config_json.type() != Json::Type::OBJECT) {
            error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                absl::StrCat("child policy configuration of \"", it->first,
                             "\" is not object")
                    .c_str()));
            continue;
          }
          Json::Object* child_policy_config =
              child_policy_config_json.mutable_object();
          (*child_policy_config)[parsed_child_policy_config_target_field_name] =
              parsed_default_target.empty() ? kDummyTargetFieldValue
                                            : parsed_default_target;
        }
      }
      parsed_default_child_policy_parsed_config =
          LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
              new_child_policy_array_json, &internal_error);
      if (internal_error != GRPC_ERROR_NONE) {
        error_list.push_back(internal_error);
      } else {
        for (const Json& child_policy_json : *new_child_policy_array) {
          if (child_policy_json.type() == Json::Type::OBJECT &&
              child_policy_json.object_value().size() == 1 &&
              child_policy_json.object_value().begin()->first ==
                  parsed_default_child_policy_parsed_config->name()) {
            Json selected_child_policy_config = std::move(child_policy_json);
            new_child_policy_array->clear();
            new_child_policy_array->push_back(
                std::move(selected_child_policy_config));
            // Intentionally left the default target in the child policy config
            // for easier processing in UpdateLocked().
            parsed_child_policy_config = std::move(*new_child_policy_array);
            break;
          }
        }
      }


std::string ParseJsonMethodName(size_t idx, const Json& json,
                                grpc_error_handle* error) {
  if (json.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("field:names index:", idx, " error:type is not object"));
    return "";
  }
  std::vector<grpc_error_handle> error_list;
  // Find service name.
  absl::string_view service_name;
  ParseJsonObjectField(json.object_value(), "service", &service_name,
                       &error_list);
  // Find method name.
  absl::string_view method_name;
  ParseJsonObjectField(json.object_value(), "method", &method_name,
                       &error_list, /*required=*/false);
  // Return error, if any.
  *error = GRPC_ERROR_CREATE_FROM_VECTOR_AND_CPP_STRING(
      absl::StrCat("field:names index:", idx), &error_list);
  // Construct path.
  return absl::StrCat("/", service_name, "/", method_name);
}

grpc_error_handle ParseGrpcKeybuilder(
    size_t idx, const Json& json, KeyBuilderMap* key_builder_map) {
  if (key_builder.type() != Json::Type::OBJECT) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("field:grpc_keybuilders index:", idx,
                     " error:is not of type object"));
  }
  // Parse names.
  std::set<std::string> names;
  const Json::Array* names_array = nullptr;
  ParseJsonObjectField(json.object_value(), "names", &names_array, &error_list);
  if (names_array != nullptr) {
    if (names_array->empty()) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:names error:list is empty"));
    } else {
      size_t name_idx = 0;
      for (const Json& name_json : *names_array) {
        grpc_error_handle child_error = GRPC_ERROR_NONE;
        std::string name =
            ParseJsonMethodName(++name_idx, name_json, &child_error);
        if (child_error != GRPC_ERROR_NONE) {
          error_list.push_back(child_error);
        } else {
          bool inserted = names.insert(std::move(name)).second;
          if (!inserted) {
            errors.push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
                absl::StrCat("field:names error:duplicate entry for ", name)));
          }
        }
      }
    }
  }
  // Parse headers.
  RlsLbConfig::KeyBuilder key_builder;
  const Json::Array* headers_array = nullptr;
  ParseJsonObjectField(json.object_value(), "headers", &headers_array,
                       &error_list, /*required=*/false);
  if (headers_array != nullptr) {
    size_t header_idx = 0;
    for (const Json& header_json : *headers_array) {
      std::string key;
      std::vector<std::string> headers;
      grpc_error_handle child_error =
          ParseJsonHeaders(++header_idx, header_json, &key, &headers);
      if (child_error != GRPC_ERROR_NONE) {
        error_list.push_back(child_error);
      } else {
        bool inserted =
            key_builder.emplace(key, std::move(headers)).second;
        if (!inserted) {
          errors.push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
              absl::StrCat("field:headers error:duplicate key for ", key)));
        }
      }
    }
  }
  // Insert key_builder into key_builder_map.
  for (std::string& name : names) {
    bool inserted =
        key_builder_map->emplace(name, std::move(key_builder)).second;
    if (!inserted) {
      errors.push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
          absl::StrCat("field:grpcKeybuilders error:duplicate name: ", name)));
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR_AND_CPP_STRING(
      absl::StrCat("field:grpc_keybuilders index:", idx), &error_list);
}

// FIXME: extract what we need from this and remove
#if 0
RlsLb::KeyMapBuilder::KeyMapBuilder(const Json& config, grpc_error_handle* error) {
  *error = GRPC_ERROR_NONE;
  if (config.type() == Json::Type::JSON_NULL) return;
  if (config.type() != Json::Type::ARRAY) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"headers\" field is not an array");
    return;
  }
  absl::InlinedVector<grpc_error_handle, 1> error_list;
  grpc_error_handle internal_error = GRPC_ERROR_NONE;
  const Json::Array& headers = config.array_value();
  int idx = 0;
  for (const Json& name_matcher_json : headers) {
    ++idx;
    if (name_matcher_json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("\"headers\" array element ", (idx - 1),
                       " is not an object")
              .c_str()));
      continue;
    }
    const Json::Object& name_matcher = name_matcher_json.object_value();
    auto required_match_json = name_matcher.find("required_match");
    if (required_match_json != name_matcher.end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"required_match\" field should not be set"));
    }
    const std::string* key_ptr =
        ParseStringFieldFromJsonObject(name_matcher, "key", &internal_error);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
      continue;
    }
    if (pattern_.find(*key_ptr) != pattern_.end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("duplicate key \"", *key_ptr, "\"").c_str()));
    }
    const Json::Array* names_ptr = ParseArrayFieldFromJsonObject(
        name_matcher, "names", &internal_error, true);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
      continue;
    }
    int idx2 = 0;
    for (const Json& name_json : *names_ptr) {
      if (name_json.type() != Json::Type::STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("\"names\" array element ,", idx2,
                         " is not a string")
                .c_str()));
      } else {
        const std::string& name = name_json.string_value();
        pattern_[*key_ptr].push_back(name);
      }
      idx2++;
    }
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR(
      "errors parsing RLS key builder config", &error_list);
}
#endif

RlsLbConfig::KeyBuilderMap ParseGrpcKeybuilders(
    const Json::Array& config, grpc_error_handle* error) {
  KeyBuilderMap key_builder_map;
  if (config.empty()) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"grpcKeybuilders\" list is empty");
    return {};
  }
  std::vector<grpc_error_handle> error_list;
  size_t idx = 0;
  for (const Json& key_builder : config) {
    grpc_error_handle internal_error =
        ParseGrpcKeybuilder(idx++, key_builder, &key_builder_map);
    if (internal_error != GRPC_ERROR_NONE) error_list.push_back(error);
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR(
      "errors parsing RLS key builders config", &error_list);
  return key_builder_map;
}

RlsLbConfig::RouteLookupConfig ParseRouteLookupConfig(
    const Json::Object& json, grpc_error_handle* error) {
  std::vector<grpc_error_handle> error_list;
  RlsLbConfig::RouteLookupConfig route_lookup_config;
  // Parse grpcKeybuilders.
  const Json::Array* keybuilder_list = nullptr;
  ParseJsonObjectField(json, "grpcKeybuilders", &keybuilder_list, &error_list);
  if (keybuilder_list != nullptr) {
    grpc_error_handle child_error = GRPC_ERROR_NONE;
    route_lookup_config.key_builder_map =
        ParseGrpcKeybuilders(*key_builder_map, &child_error);
    if (child_error != GRPC_ERROR_NONE) error_list.push_back(child_error);
  }
  // Parse lookupService.
  if (ParseJsonObjectField(json, "lookupService",
                           &route_lookup_config.lookup_service, &error_list)) {
    if (!ResolverRegistry::IsValidTarget(route_lookup_config.lookup_servic)) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:lookupService error:must be valid gRPC target URI"));
    }
  }
  // Parse lookupServiceTimeout.
  ParseJsonObjectFieldAsDuration(json, "lookupServiceTimeout",
                                 &route_lookup_config.lookup_service_timeout,
                                 &error_list, /*required=*/false);
  // Parse maxAge.
  route_lookup_config.max_age = kMaxMaxAge;
  bool max_age_set = ParseJsonObjectFieldAsDuration(
      json, "maxAge", &route_lookup_config.max_age, &error_list,
      /*required=*/false);
  // Parse staleAge.
  bool stale_age_set = ParseJsonObjectFieldAsDuration(
      json, "staleAge", &route_lookup_config.stale_age, &error_list,
      /*required=*/false);
  // If staleAge is set, then maxAge must also be set.
  if (stale_age_set && !max_age_set) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:maxAge error:must be set if staleAge is set"));
  }
  // Ignore staleAge if greater than or equal to maxAge.
  if (route_lookup_config.stale_age >= route_lookup_config.max_age) {
    route_lookup_config.stale_age = 0;
  }
  // Parse cacheSizeBytes.
  route_lookup_config.cache_size_bytes = kDefaultCacheSizeBytes;
  ParseJsonObjectField(json, "cacheSizeBytes",
                       &route_lookup_config.cache_size_bytes, &error_list,
                       /*required=*/false);
  if (route_lookup_config.cache_size_bytes <= 0) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:cacheSizeBytes error:must be greater than 0"));
  }
  // Parse defaultTarget.
  if (ParseJsonObjectField(json, "defaultTarget",
                           &route_lookup_config.default_target, &error_list,
                           /*required=*/false)) {
    if (route_lookup_config.default_target.empty()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:defaultTarget error:must be non-empty if set"));
    }
  }
  return route_lookup_config;
}

}  // namespace

RefCountedPtr<LoadBalancingPolicy::Config>
RlsLbFactory::ParseLoadBalancingConfig(const Json& config_json,
                                       grpc_error_handle* error) const {
  std::vector<grpc_error_handle> error_list;
  // Parse routeLookupConfig.
  RlsLbConfig::RouteLookupConfig route_lookup_config;
  const Json::Object* route_lookup_config_json = nullptr;
  if (ParseJsonObjectField(config_json, "routeLookupConfig",
                           &route_lookup_config_json, &error_list)) {
    grpc_error_handle child_error = GRPC_ERROR_NONE;
    route_lookup_config =
        ParseRouteLookupConfig(*route_lookup_config_json, &child_error);
    if (child_error != GRPC_ERROR_NONE) error_list.push_back(child_error);
  }
  // Parse childPolicyConfigTargetFieldName.
  std::string child_policy_config_target_field_name;
  ParseJsonObjectField(config_json, "childPolicyConfigTargetFieldName",
                       &child_policy_config_target_field_name, &error_list);
  if (child_policy_config_target_field_name.empty()) {
    errors.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "field:childPolicyConfigTargetFieldName error:must be non-empty"));
  }
  // Parse childPolicy.
  Json::Array* child_policy_list = nullptr;
  ParseJsonObjectField(config_json, "childPolicy", &child_policy_list,
                       &error_list);
  // Validate child policy config.
  Json child_policy_config;
  if (child_policy_list != nullptr) {
    grpc_error_handle child_error = GRPC_ERROR_NONE;
  child_policy_config =
        ValidateChildPolicyList(*child_policy_list, &child_error);
    if (child_error != GRPC_ERROR_NONE) error_list.push_back(child_error);
  }
  // Construct default child policy if needed.
  RefCountedPtr<LoadBalancingPolicy::Config>
      parsed_default_child_policy_parsed_config;
// FIXME
  *error = GRPC_ERROR_CREATE_FROM_VECTOR(
      "errors parsing RLS LB policy config", &error_list);
  return MakeRefCounted<RlsLbConfig>(
      std::move(route_lookup_config),
      std::move(default_child_policy_parsed_config),
      std::move(child_policy_config),
      std::move(child_policy_config_target_field_name));
}

}  // namespace grpc_core

void grpc_lb_policy_rls_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::RlsLbFactory>());
}

void grpc_lb_policy_rls_shutdown() {}
