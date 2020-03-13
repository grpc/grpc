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

#include <stdlib.h>
#include <algorithm>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy/rls/rls.h"
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/upb-generated/src/proto/grpc/lookup/rls.upb.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/gpr/random.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/string_view.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/static_metadata.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

TraceFlag grpc_lb_rls_trace(false, "rls");

static const char* kRls = "rls";
static const char* kGrpc = "grpc";
static const char* kRlsRequestPath =
    "/grpc.lookup.v1.RouteLookupService/RouteLookup";
static const char* kDummyTargetFieldValue = "dummy_target_field_value";
static const std::string kWildcardMethod("*");
static const char* kRlsHeaderKey = "X-Google-RLS-Data";

static const grpc_millis kDefaultLookupServiceTimeout = 10000;
static const grpc_millis kMaxMaxAge = 5 * 60 * GPR_MS_PER_SEC;
static const int64_t kDefaultCacheSizeBytes = 10 * 1024 * 1024;
static const grpc_millis kMinExpirationTime = 5 * GPR_MS_PER_SEC;
static const grpc_millis kCacheBackoffInitial = 1 * GPR_MS_PER_SEC;
static const double kCacheBackoffMultiplier = 1.6;
static const double kCacheBackoffJitter = 0.2;
static const grpc_millis kCacheBackoffMax = 120 * GPR_MS_PER_SEC;
static const grpc_millis kDefaultThrottleWindowSize = 30 * GPR_MS_PER_SEC;
static const double kDefaultThrottleRatioForSuccesses = 2.0;
static const int kDefaultThrottlePaddings = 8;
static const grpc_millis kCacheCleanupTimerInterval = 60 * GPR_MS_PER_SEC;

inline static const Json* ParseFieldJsonFromJsonObject(
    const Json::Object& object, const std::string& field, grpc_error** error,
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

static const Json::Object* ParseObjectFieldFromJsonObject(
    const Json::Object& object, const std::string& field, grpc_error** error,
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

static const Json::Array* ParseArrayFieldFromJsonObject(
    const Json::Object& object, const std::string& field, grpc_error** error,
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

static const std::string* ParseStringFieldFromJsonObject(
    const Json::Object& object, const std::string& field, grpc_error** error,
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

static int64_t ParseNumberFieldFromJsonObject(const Json::Object& object,
                                              const std::string& field,
                                              grpc_error** error,
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

inline static grpc_millis ParseDuration(const Json::Object& duration_object,
                                        grpc_error** error) {
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

static grpc_error* InsertOrUpdateChildPolicyField(Json* config,
                                                  const std::string& field,
                                                  const std::string& value) {
  if (config->type() != Json::Type::ARRAY) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "child policy configuration is not an array");
  }
  InlinedVector<grpc_error*, 1> error_list;
  for (auto& child_json : *config->mutable_array()) {
    if (child_json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "child policy item is not an object"));
    } else {
      auto& child = *child_json.mutable_object();
      if (child.size() != 1) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "child policy item contains more than one field"));
      } else {
        auto& child_config_json = child.begin()->second;
        if (child_config_json.type() != Json::Type::OBJECT) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "child policy item config is not an object"));
        } else {
          auto& child_config = *child_config_json.mutable_object();
          child_config[field] = Json(value);
        }
      }
    }
  }

  grpc_error* result = GRPC_ERROR_CREATE_FROM_VECTOR(
      absl::StrCat("errors when inserting field \"", field,
                   "\" for child policy")
          .c_str(),
      &error_list);
  return result;
}

RlsLb::KeyMapBuilderMap RlsCreateKeyMapBuilderMap(const Json& config,
                                                  grpc_error** error) {
  *error = GRPC_ERROR_NONE;
  RlsLb::KeyMapBuilderMap result;
  grpc_error* internal_error = GRPC_ERROR_NONE;

  if (config.type() != Json::Type::ARRAY) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "malformed RLS JSON configuration");
    return RlsLb::KeyMapBuilderMap();
  }
  InlinedVector<grpc_error*, 1> error_list;
  if (config.array_value().empty()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"grpc_keybuilders\" field is empty"));
  }
  int idx = 0;
  for (auto& key_builder_json : config.array_value()) {
    if (key_builder_json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("\"grpc_keybuilders\" array element ", idx,
                       " is not an object")
              .c_str()));
    }
    auto& key_builder = key_builder_json.object_value();
    auto names_ptr =
        ParseArrayFieldFromJsonObject(key_builder, "names", &internal_error);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else {
      auto& names = *names_ptr;
      if (names.empty()) {
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("\"names\" field is empty"));
      }
      int idx2 = 0;
      for (auto& name_json : names) {
        if (name_json.type() != Json::Type::OBJECT) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
              absl::StrCat("\"names\" array element ", idx2,
                           " is not an object")
                  .c_str()));
          continue;
        }
        auto& name = name_json.object_value();
        const std::string* service =
            ParseStringFieldFromJsonObject(name, "service", &internal_error);
        if (internal_error != GRPC_ERROR_NONE || service->length() == 0) {
          error_list.push_back(internal_error);
        }
        const std::string* method = ParseStringFieldFromJsonObject(
            name, "method", &internal_error, true);
        if (internal_error != GRPC_ERROR_NONE) {
          error_list.push_back(internal_error);
        } else if (method == nullptr || method->length() == 0) {
          method = &kWildcardMethod;
        }
        if (service != nullptr && method != nullptr) {
          std::stringstream ss;
          ss << '/' << *service << '/' << *method;
          if (result.find(ss.str()) != result.end()) {
            error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                absl::StrCat("duplicate name ", ss.str()).c_str()));
          } else {
            auto headers_json_ptr = ParseFieldJsonFromJsonObject(
                key_builder, "headers", &internal_error, true);
            if (internal_error != GRPC_ERROR_NONE) {
              error_list.push_back(internal_error);
            } else {
              RlsLb::KeyMapBuilder builder = RlsLb::KeyMapBuilder(
                  headers_json_ptr == nullptr ? Json() : *headers_json_ptr,
                  &internal_error);
              if (internal_error) {
                error_list.push_back(internal_error);
              } else {
                result.insert(std::make_pair(ss.str(), std::move(builder)));
              }
            }
          }
        }
        idx2++;
      }
    }
    idx++;
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR(
      "errors parsing RLS key builders config", &error_list);
  return result;
}

const RlsLb::KeyMapBuilder* RlsFindKeyMapBuilder(
    const RlsLb::KeyMapBuilderMap& key_map_builder_map,
    const std::string& path) {
  auto it = key_map_builder_map.find(std::string(path));
  if (it == key_map_builder_map.end()) {
    auto last_slash_pos = path.rfind("/");
    GPR_DEBUG_ASSERT(last_slash_pos != path.npos);
    if (GPR_UNLIKELY(last_slash_pos == path.npos)) {
      return nullptr;
    }
    std::string path_wildcard = std::string(path, 0, last_slash_pos + 1);
    path_wildcard += kWildcardMethod;

    it = key_map_builder_map.find(path_wildcard);
    if (it == key_map_builder_map.end()) {
      return nullptr;
    }
  }
  return &(it->second);
}

std::string RlsFindPathFromMetadata(
    LoadBalancingPolicy::MetadataInterface* metadata) {
  for (auto it = metadata->begin(); it != metadata->end(); ++it) {
    if ((*it).first == ":path") {
      return std::string((*it).second);
    }
  }
  return "";
}

bool RlsLb::Key::operator==(const Key& rhs) const {
  return (path == rhs.path && key_map == rhs.key_map);
}

size_t RlsLb::KeyHasher::operator()(const Key& key) const {
  std::hash<std::string> string_hasher;
  size_t result = key.key_map.size();
  result ^= string_hasher(key.path);
  for (auto& kv : key.key_map) {
    result ^= string_hasher(kv.first);
    result ^= string_hasher(kv.second);
  }
  return result;
}

// Picker implementation
LoadBalancingPolicy::PickResult RlsLb::Picker::Pick(PickArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[rlslb %p] picker=%p: picker pick", lb_policy_.get(),
            this);
  }
  Key key;
  key.path = RlsFindPathFromMetadata(args.initial_metadata);
  std::lock_guard<std::recursive_mutex> lock(lb_policy_->mu_);
  if (lb_policy_->is_shutdown_) {
    PickResult result;
    result.type = PickResult::PICK_FAILED;
    result.error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("LB policy already shut down");
    return result;
  }
  auto key_map_builder = lb_policy_->FindKeyMapBuilder(key.path);
  if (key_map_builder != nullptr) {
    key.key_map = key_map_builder->BuildKeyMap(args.initial_metadata);
  }
  auto entry = lb_policy_->cache_.Find(key);
  if (entry == nullptr) {
    bool call_throttled = !lb_policy_->MaybeMakeRlsCall(key);
    if (call_throttled) {
      switch (lb_policy_->current_config_->request_processing_strategy()) {
        case RequestProcessingStrategy::SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR:
        case RequestProcessingStrategy::ASYNC_LOOKUP_DEFAULT_TARGET_ON_MISS:
          if (lb_policy_->default_child_policy_->child()->IsReady()) {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
              gpr_log(GPR_DEBUG,
                      "[rlslb %p] picker=%p: pick forwarded to default target "
                      "as the RLS call is throttled",
                      lb_policy_.get(), this);
            }
            return lb_policy_->default_child_policy_->child()->Pick(
                std::move(args));
          } else {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
              gpr_log(GPR_DEBUG,
                      "[rlslb %p] picker=%p: pick queued as the RLS call is "
                      "throttled but the default target is not ready",
                      lb_policy_.get(), this);
            }
            PickResult result;
            result.type = PickResult::PICK_QUEUE;
            return result;
          }
          break;
        case RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR: {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
            gpr_log(GPR_DEBUG,
                    "[rlslb %p] picker=%p: pick failed as the RLS call is "
                    "throttled",
                    lb_policy_.get(), this);
          }
          PickResult result;
          result.type = PickResult::PICK_FAILED;
          result.error = grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("RLS request throttled"),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
          return result;
        }
        default:
          abort();
      }
    } else {
      switch (lb_policy_->current_config_->request_processing_strategy()) {
        case RequestProcessingStrategy::SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR:
        case RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR: {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
            gpr_log(GPR_DEBUG,
                    "[rlslb %p] picker=%p: pick queued as the RLS call is made",
                    lb_policy_.get(), this);
          }
          PickResult result;
          result.type = PickResult::PICK_QUEUE;
          return result;
        }
        case RequestProcessingStrategy::ASYNC_LOOKUP_DEFAULT_TARGET_ON_MISS:
          if (lb_policy_->default_child_policy_->child()->IsReady()) {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
              gpr_log(GPR_DEBUG,
                      "[rlslb %p] picker=%p: pick forwarded to default target; "
                      "RLS call is made",
                      lb_policy_.get(), this);
            }
            return lb_policy_->default_child_policy_->child()->Pick(
                std::move(args));
          } else {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
              gpr_log(GPR_DEBUG,
                      "[rlslb %p] picker=%p: pick queued as the RLS call is "
                      "made but the default target is not ready",
                      lb_policy_.get(), this);
            }
            PickResult result;
            result.type = PickResult::PICK_QUEUE;
            return result;
          }
        default:
          abort();
      }
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
    : lb_policy_(lb_policy),
      min_expiration_time_(ExecCtx::Get()->Now() + kMinExpirationTime) {
  BackOff::Options backoff_options;
  backoff_options.set_initial_backoff(kCacheBackoffInitial)
      .set_multiplier(kCacheBackoffMultiplier)
      .set_jitter(kCacheBackoffJitter)
      .set_max_backoff(kCacheBackoffMax);
  backoff_state_ = std::unique_ptr<BackOff>(new BackOff(backoff_options));
  GRPC_CLOSURE_INIT(&backoff_timer_callback_, OnBackoffTimer,
                    reinterpret_cast<void*>(this), nullptr);
  GRPC_CLOSURE_INIT(&backoff_timer_combiner_callback_, OnBackoffTimerLocked,
                    reinterpret_cast<void*>(this), nullptr);
}

LoadBalancingPolicy::PickResult RlsLb::Cache::Entry::Pick(PickArgs args) {
  PickResult result;

  grpc_millis now = ExecCtx::Get()->Now();
  if (stale_time_ < now && backoff_time_ < now) {
    bool call_throttled =
        !lb_policy_->MaybeMakeRlsCall(*lru_iterator_, &backoff_state_);
    if (call_throttled && data_expiration_time_ < now) {
      switch (lb_policy_->current_config_->request_processing_strategy()) {
        case RequestProcessingStrategy::SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR:
        case RequestProcessingStrategy::ASYNC_LOOKUP_DEFAULT_TARGET_ON_MISS:
          if (lb_policy_->default_child_policy_->child()->IsReady()) {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
              gpr_log(GPR_DEBUG,
                      "[rlslb %p] picker=%p: pick forwarded to default target "
                      "as the RLS call is throttled",
                      lb_policy_.get(), this);
            }
            return lb_policy_->default_child_policy_->child()->Pick(
                std::move(args));
          } else {
            if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
              gpr_log(GPR_DEBUG,
                      "[rlslb %p] picker=%p: pick queued as the RLS call is "
                      "throttled but the default target is not ready",
                      lb_policy_.get(), this);
            }
            result.type = PickResult::PICK_QUEUE;
            return result;
          }
        case RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR:
          if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
            gpr_log(GPR_DEBUG,
                    "[rlslb %p] picker=%p: pick failed as the RLS call is "
                    "throttled",
                    lb_policy_.get(), this);
          }
          result.type = PickResult::PICK_FAILED;
          result.error = grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("RLS request throttled"),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
          return result;
        default:
          abort();
      }
    }
  }
  if (now <= data_expiration_time_) {
    GPR_DEBUG_ASSERT(child_policy_wrapper_ != nullptr);
    if (child_policy_wrapper_ == nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
        gpr_log(GPR_ERROR,
                "[rlslb %p] cache entry=%p: cached response is valid but child "
                "policy wrapper is empty",
                lb_policy_.get(), this);
      }
      result.type = PickResult::PICK_FAILED;
      result.error =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("child policy does not exist");
    } else if (!child_policy_wrapper_->child()->IsReady()) {
      switch (lb_policy_->current_config_->request_processing_strategy()) {
        case RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR:
        case RequestProcessingStrategy::SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR:
          if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
            gpr_log(GPR_DEBUG,
                    "[rlslb %p] cache entry=%p: pick queued as child policy is "
                    "not ready",
                    lb_policy_.get(), this);
          }
          result.type = PickResult::PICK_QUEUE;
          break;
        case RequestProcessingStrategy::ASYNC_LOOKUP_DEFAULT_TARGET_ON_MISS:
          if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
            gpr_log(GPR_DEBUG,
                    "[rlslb %p] cache entry=%p: pick forwarded to the default "
                    "child policy as child policy is not ready",
                    lb_policy_.get(), this);
          }
          return lb_policy_->default_child_policy_->child()->Pick(args);
        default:
          abort();
      }
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
        gpr_log(GPR_DEBUG,
                "[rlslb %p] cache entry=%p: pick forwarded to child policy %p",
                lb_policy_.get(), this, child_policy_wrapper_->child());
      }
      if (!header_data_.empty()) {
        char* copied_header_data = static_cast<char*>(
            args.call_state->Alloc(header_data_.length() + 1));
        strcpy(copied_header_data, header_data_.c_str());
        args.initial_metadata->Add(kRlsHeaderKey, copied_header_data);
      }
      result = child_policy_wrapper_->child()->Pick(args);
      if (result.type != PickResult::PICK_COMPLETE) {
        for (auto it = args.initial_metadata->begin();
             it != args.initial_metadata->end(); ++it) {
          if ((*it).first == kRlsHeaderKey) {
            args.initial_metadata->erase(it);
            break;
          }
        }
      }
      if (result.type == PickResult::PICK_FAILED) {
        return lb_policy_->default_child_policy_->child()->Pick(args);
      } else {
        return result;
      }
    }
  } else if (now <= backoff_time_) {
    switch (lb_policy_->current_config_->request_processing_strategy()) {
      case RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR:
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[rlslb %p] cache entry=%p: pick failed due to backoff",
                  lb_policy_.get(), this);
        }
        result.type = PickResult::PICK_FAILED;
        result.error = grpc_error_add_child(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("RLS request in backoff"),
            GRPC_ERROR_REF(status_));
        break;
      case RequestProcessingStrategy::SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR:
      case RequestProcessingStrategy::ASYNC_LOOKUP_DEFAULT_TARGET_ON_MISS:
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[rlslb %p] cache entry=%p: pick forwarded to the default "
                  "child policy",
                  lb_policy_.get(), this);
        }
        return lb_policy_->default_child_policy_->child()->Pick(args);
      default:
        abort();
    }
  } else {
    switch (lb_policy_->current_config_->request_processing_strategy()) {
      case RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR:
      case RequestProcessingStrategy::SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR:
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[rlslb %p] cache entry=%p: pick queued and started "
                  "refreshing request",
                  lb_policy_.get(), this);
        }
        result.type = PickResult::PICK_QUEUE;
        break;
      case RequestProcessingStrategy::ASYNC_LOOKUP_DEFAULT_TARGET_ON_MISS:
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[rlslb %p] cache entry=%p: pick forwarded to the default "
                  "child policy",
                  lb_policy_.get(), this);
        }
        return lb_policy_->default_child_policy_->child()->Pick(args);
      default:
        abort();
    }
  }
  return result;
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
    status_ = nullptr;
  }
  backoff_state_.reset();
  if (timer_pending_) {
    grpc_timer_cancel(&backoff_timer_);
  }
  child_policy_wrapper_.reset();
}

void RlsLb::Cache::Entry::OnRlsResponseLocked(
    ResponseInfo response, std::unique_ptr<BackOff> backoff_state) {
  if (response.error == GRPC_ERROR_NONE) {
    if (child_policy_wrapper_ != nullptr &&
        child_policy_wrapper_->child()->target() == response.target) {
      if (lb_policy_->current_config_->request_processing_strategy() ==
              RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR ||
          lb_policy_->current_config_->request_processing_strategy() ==
              RequestProcessingStrategy::SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR) {
        lb_policy_->UpdatePickerLocked();
      }
    } else {
      auto it = lb_policy_->child_policy_map_.find(response.target);
      if (it == lb_policy_->child_policy_map_.end()) {
        child_policy_wrapper_ = RefCountedPtr<ChildPolicyWrapper::RefHandler>(
            new ChildPolicyWrapper::RefHandler(
                new ChildPolicyWrapper(lb_policy_->Ref(), response.target)));
        Json copied_child_policy_config =
            lb_policy_->current_config_->child_policy_config();
        grpc_error* error = InsertOrUpdateChildPolicyField(
            &copied_child_policy_config,
            lb_policy_->current_config_
                ->child_policy_config_target_field_name(),
            response.target);
        GPR_ASSERT(error == GRPC_ERROR_NONE);
        child_policy_wrapper_->child()->UpdateLocked(
            copied_child_policy_config, lb_policy_->current_addresses_,
            lb_policy_->current_channel_args_);
      } else {
        child_policy_wrapper_ = it->second->Ref();
        if (lb_policy_->current_config_->request_processing_strategy() ==
                RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR ||
            lb_policy_->current_config_->request_processing_strategy() ==
                RequestProcessingStrategy::
                    SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR) {
          lb_policy_->UpdatePickerLocked();
        }
      }
    }
    header_data_ = std::move(response.header_data);
    grpc_millis now = ExecCtx::Get()->Now();
    data_expiration_time_ = now + lb_policy_->current_config_->max_age();
    stale_time_ = now + lb_policy_->current_config_->stale_age();
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
    if (lb_policy_->current_config_->request_processing_strategy() ==
        RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR) {
      timer_pending_ = true;
      Ref().release();
      grpc_timer_init(&backoff_timer_, backoff_time_, &backoff_timer_callback_);
    }
    if (lb_policy_->current_config_->request_processing_strategy() ==
            RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR ||
        lb_policy_->current_config_->request_processing_strategy() ==
            RequestProcessingStrategy::SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR) {
      lb_policy_->UpdatePickerLocked();
    }
  }

  // move the entry to the end of the LRU list
  auto& cache = lb_policy_->cache_;
  cache.lru_list_.push_back(*lru_iterator_);
  cache.lru_list_.erase(lru_iterator_);
  lru_iterator_ = cache.lru_list_.end();
  lru_iterator_--;
}

void RlsLb::Cache::Entry::set_iterator(RlsLb::Cache::Iterator iterator) {
  lru_iterator_ = iterator;
}

RlsLb::Cache::Iterator RlsLb::Cache::Entry::iterator() const {
  return lru_iterator_;
}

void RlsLb::Cache::Entry::OnBackoffTimerLocked(void* arg, grpc_error* error) {
  (void)error;
  RefCountedPtr<Entry> entry(reinterpret_cast<Entry*>(arg));
  std::lock_guard<std::recursive_mutex> lock(entry->lb_policy_->mu_);
  entry->timer_pending_ = false;
  if (entry->is_shutdown_) return;
  entry->lb_policy_->UpdatePickerLocked();
}

void RlsLb::Cache::Entry::OnBackoffTimer(void* arg, grpc_error* error) {
  Entry* entry = reinterpret_cast<Entry*>(arg);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(
        GPR_DEBUG,
        "[rlslb %p] cache entry=%p, error=%p: successful RLS response received",
        entry->lb_policy_.get(), entry, error);
  }
  if (error == GRPC_ERROR_CANCELLED) {
    entry->lb_policy_->mu_.lock();
    entry->timer_pending_ = false;
    entry->lb_policy_->mu_.unlock();
    entry->Unref();
  } else {
    entry->lb_policy_->combiner()->Run(&entry->backoff_timer_combiner_callback_,
                                       GRPC_ERROR_REF(error));
  }
}

RlsLb::Cache::Cache(RlsLb* lb_policy) : lb_policy_(lb_policy) {
  grpc_millis now = ExecCtx::Get()->Now();
  lb_policy_->Ref().release();
  GRPC_CLOSURE_INIT(&timer_callback_, OnCleanupTimer,
                    reinterpret_cast<void*>(this), nullptr);
  grpc_timer_init(&cleanup_timer_, now + kCacheCleanupTimerInterval,
                  &timer_callback_);
}

RlsLb::Cache::Entry* RlsLb::Cache::Find(Key key) {
  auto it = map_.find(key);
  if (it == map_.end()) {
    return nullptr;
  } else {
    auto lru_it = it->second->iterator();
    lru_list_.push_back(*lru_it);
    lru_list_.erase(lru_it);
    lru_it = lru_list_.end();
    lru_it--;
    it->second->set_iterator(lru_it);
    return it->second.get();
  }
}

void RlsLb::Cache::Add(Key key, OrphanablePtr<Entry> entry) {
  auto it = map_.find(key);
  if (GPR_UNLIKELY(it != map_.end())) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
      gpr_log(GPR_INFO, "[rlslb %p] cache entry add failed with existing entry",
              lb_policy_);
    }
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
      gpr_log(GPR_DEBUG, "[rlslb %p] cache entry added, entry=%p", lb_policy_,
              entry.get());
    }
    lru_list_.push_back(key);
    entry->set_iterator(--(lru_list_.end()));
    map_.emplace(std::move(key), std::move(entry));
    if (static_cast<int>(map_.size()) > element_size_) {
      auto lru_it = lru_list_.begin();
      auto map_it = map_.find(*lru_it);
      GPR_DEBUG_ASSERT(map_it != map_.end());
      if (map_it != map_.end()) {
        map_.erase(map_it);
      }
      lru_list_.erase(lru_it);
    }
  }
}

void RlsLb::Cache::Resize(int64_t bytes) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[rlslb %p] bytes=%" PRId64 ": cache resized",
            lb_policy_, bytes);
  }
  // take ceiling if the result is a fraction
  element_size_ = (bytes + kCacheEntrySize - 1) / kCacheEntrySize;
  size_bytes_ = bytes;
  if (element_size_ >= static_cast<int>(map_.size())) return;
  int to_drop_size = map_.size() - element_size_;

  auto it = lru_list_.begin();
  for (int i = 0; i < to_drop_size; i++) {
    GPR_DEBUG_ASSERT(it != lru_list_.end());
    if (it == lru_list_.end()) break;

    auto map_it = map_.find(*it);
    GPR_DEBUG_ASSERT(map_it != map_.end());
    if (GPR_UNLIKELY(map_it != map_.end())) {
      map_.erase(map_it);
    }
    it = lru_list_.erase(it);
  }
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

void RlsLb::Cache::OnCleanupTimer(void* arg, grpc_error* error) {
  Cache* cache = reinterpret_cast<Cache*>(arg);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[rlslb %p] cache=%p, error=%p: cleanup timer fired",
            cache->lb_policy_, cache, error);
  }
  if (error == GRPC_ERROR_CANCELLED) {
    cache->lb_policy_->Unref();
    return;
  }
  std::lock_guard<std::recursive_mutex> lock(cache->lb_policy_->mu_);
  if (cache->lb_policy_->is_shutdown_) return;
  for (auto it = cache->map_.begin(); it != cache->map_.end();) {
    if (GPR_UNLIKELY(it->second->ShouldRemove())) {
      auto lru_it = it->second->iterator();
      auto temp_it = it;
      it++;
      cache->map_.erase(temp_it);
      cache->lru_list_.erase(lru_it);
    } else {
      it++;
    }
  }
  grpc_millis now = ExecCtx::Get()->Now();
  grpc_timer_init(&cache->cleanup_timer_, now + kCacheCleanupTimerInterval,
                  &cache->timer_callback_);
}

// Request map implementation

RlsLb::RequestMapEntry::RequestMapEntry(RefCountedPtr<RlsLb> lb_policy, Key key,
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
  GRPC_CLOSURE_INIT(&call_complete_cb_, OnRlsCallComplete,
                    reinterpret_cast<void*>(this), nullptr);
  GRPC_CLOSURE_INIT(&call_complete_locked_cb_, OnRlsCallCompleteLocked,
                    reinterpret_cast<void*>(this), nullptr);
  ExecCtx::Run(
      DEBUG_LOCATION,
      GRPC_CLOSURE_INIT(&call_start_cb_, StartCall,
                        reinterpret_cast<void*>(Ref().release()), nullptr),
      GRPC_ERROR_NONE);
}

RlsLb::RequestMapEntry::~RequestMapEntry() {
  if (call_ != nullptr) {
    GRPC_CALL_INTERNAL_UNREF(call_, "request map destroyed");
  }
  grpc_byte_buffer_destroy(message_send_);
  grpc_byte_buffer_destroy(message_recv_);
  grpc_metadata_array_destroy(&initial_metadata_recv_);
  grpc_metadata_array_destroy(&trailing_metadata_recv_);
  grpc_slice_unref_internal(status_details_recv_);
}

void RlsLb::RequestMapEntry::Orphan() {
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

void RlsLb::RequestMapEntry::StartCall(void* arg, grpc_error* error) {
  (void)error;
  RefCountedPtr<RequestMapEntry> entry(reinterpret_cast<RequestMapEntry*>(arg));

  grpc_millis now = ExecCtx::Get()->Now();
  grpc_call* call = grpc_channel_create_pollset_set_call(
      entry->channel_->channel(), nullptr, GRPC_PROPAGATE_DEFAULTS,
      entry->lb_policy_->interested_parties(),
      grpc_slice_from_static_string(kRlsRequestPath), nullptr,
      now + entry->lb_policy_->current_config_->lookup_service_timeout(),
      nullptr);
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
  entry->MakeRequestProto();
  op->data.send_message.send_message = entry->message_send_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &entry->initial_metadata_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &entry->message_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &entry->trailing_metadata_recv_;
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

void RlsLb::RequestMapEntry::OnRlsCallComplete(void* arg, grpc_error* error) {
  RequestMapEntry* entry = reinterpret_cast<RequestMapEntry*>(arg);
  entry->lb_policy_->combiner()->Run(&entry->call_complete_locked_cb_,
                                     GRPC_ERROR_REF(error));
}

void RlsLb::RequestMapEntry::OnRlsCallCompleteLocked(void* arg,
                                                     grpc_error* error) {
  RefCountedPtr<RequestMapEntry> entry(reinterpret_cast<RequestMapEntry*>(arg));
  std::lock_guard<std::recursive_mutex> lock(entry->lb_policy_->mu_);
  if (entry->lb_policy_->is_shutdown_) return;
  bool call_failed =
      (error != GRPC_ERROR_NONE || entry->status_recv_ != GRPC_STATUS_OK);

  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[rlslb %p] request_map_entry=%p, error=%p, status=%d: RLS call "
            "response received",
            entry->lb_policy_.get(), arg, error, entry->status_recv_);
  }

  entry->channel_->ReportResponseLocked(call_failed);
  ResponseInfo res;
  if (call_failed) {
    if (error == GRPC_ERROR_NONE) {
      res.error = grpc_error_set_str(
          grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("received error status"),
              GRPC_ERROR_INT_GRPC_STATUS, entry->status_recv_),
          GRPC_ERROR_STR_GRPC_MESSAGE, entry->status_details_recv_);
    } else {
      res.error = error;
    }
  } else {
    res.error = entry->ParseResponseProto(&res.target, &res.header_data);
    if (res.error == GRPC_ERROR_NONE && res.target.length() == 0) {
      res.error =
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("server returned empty target");
    }
  }
  auto cache_entry = entry->lb_policy_->cache_.Find(entry->key_);
  if (cache_entry == nullptr) {
    cache_entry = new Cache::Entry(entry->lb_policy_);
    entry->lb_policy_->cache_.Add(entry->key_,
                                  OrphanablePtr<Cache::Entry>(cache_entry));
  }
  cache_entry->OnRlsResponseLocked(std::move(res),
                                   std::move(entry->backoff_state_));
  entry->lb_policy_->request_map_.erase(entry->key_);
}

void RlsLb::RequestMapEntry::MakeRequestProto() {
  upb::Arena arena;
  grpc_lookup_v1_RouteLookupRequest* req =
      grpc_lookup_v1_RouteLookupRequest_new(arena.ptr());
  grpc_lookup_v1_RouteLookupRequest_set_server(
      req, upb_strview_make(lb_policy_->server_uri_.c_str(),
                            lb_policy_->server_uri_.length()));
  grpc_lookup_v1_RouteLookupRequest_set_path(
      req, upb_strview_make(key_.path.c_str(), key_.path.length()));
  grpc_lookup_v1_RouteLookupRequest_set_target_type(
      req, upb_strview_make(kGrpc, strlen(kGrpc)));
  for (auto& kv : key_.key_map) {
    grpc_lookup_v1_RouteLookupRequest_KeyMapEntry* key_map =
        grpc_lookup_v1_RouteLookupRequest_add_key_map(req, arena.ptr());
    grpc_lookup_v1_RouteLookupRequest_KeyMapEntry_set_key(
        key_map, upb_strview_make(kv.first.c_str(), kv.first.length()));
    grpc_lookup_v1_RouteLookupRequest_KeyMapEntry_set_value(
        key_map, upb_strview_make(kv.second.c_str(), kv.second.length()));
  }
  size_t len;
  char* buf =
      grpc_lookup_v1_RouteLookupRequest_serialize(req, arena.ptr(), &len);
  grpc_slice send_slice = grpc_slice_from_copied_buffer(buf, len);
  message_send_ = grpc_raw_byte_buffer_create(&send_slice, 1);
}

grpc_error* RlsLb::RequestMapEntry::ParseResponseProto(
    std::string* target, std::string* header_data) {
  upb::Arena arena;
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, message_recv_);
  grpc_slice recv_slice = grpc_byte_buffer_reader_readall(&bbr);
  auto res = grpc_lookup_v1_RouteLookupResponse_parse(
      reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(recv_slice)),
      GRPC_SLICE_LENGTH(recv_slice), arena.ptr());
  if (res == nullptr) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("cannot parse RLS response");
  }
  auto target_strview = grpc_lookup_v1_RouteLookupResponse_target(res);
  auto header_data_strview =
      grpc_lookup_v1_RouteLookupResponse_header_data(res);
  *target = std::string(target_strview.data, target_strview.size);
  *header_data =
      std::string(header_data_strview.data, header_data_strview.size);

  return GRPC_ERROR_NONE;
}

// ControlChannel implementation

RlsLb::ControlChannel::ControlChannel(RefCountedPtr<RlsLb> lb_policy,
                                      const std::string& target,
                                      const grpc_channel_args* channel_args)
    : lb_policy_(lb_policy) {
  grpc_channel_credentials* creds =
      grpc_channel_credentials_find_in_args(channel_args);
  channel_ =
      grpc_secure_channel_create(creds, target.c_str(), nullptr, nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[RlsLb %p] ControlChannel=%p, channel=%p: control channel created",
            lb_policy.get(), this, channel_);
  }
  if (channel_ != nullptr) {
    watcher_ = new StateWatcher(Ref());
    grpc_client_channel_start_connectivity_watch(
        grpc_channel_stack_last_element(
            grpc_channel_get_channel_stack(channel_)),
        GRPC_CHANNEL_IDLE, OrphanablePtr<StateWatcher>(watcher_));
  }
}

void RlsLb::ControlChannel::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(
        GPR_DEBUG,
        "[RlsLb %p] ControlChannel=%p, channel=%p: control channel shutdown",
        lb_policy_.get(), this, channel_);
  }
  is_shutdown_ = true;
  if (channel_ == nullptr) {
    if (watcher_ != nullptr) {
      grpc_client_channel_stop_connectivity_watch(
          grpc_channel_stack_last_element(
              grpc_channel_get_channel_stack(channel_)),
          watcher_);
      watcher_ = nullptr;
    }
    grpc_channel_destroy(channel_);
  }
}

void RlsLb::ControlChannel::ReportResponseLocked(bool response_succeeded) {
  throttle_.RegisterResponse(response_succeeded);
}

bool RlsLb::ControlChannel::ShouldThrottle() {
  return throttle_.ShouldThrottle();
}

void RlsLb::ControlChannel::ResetBackoff() {
  GPR_DEBUG_ASSERT(channel_ != nullptr);
  grpc_channel_reset_connect_backoff(channel_);
}

// Throttle implementation
RlsLb::ControlChannel::Throttle::Throttle(int window_size_seconds,
                                          double ratio_for_successes,
                                          int paddings)
    : rng_state_(static_cast<uint32_t>(gpr_now(GPR_CLOCK_REALTIME).tv_nsec)) {
  GPR_DEBUG_ASSERT(window_size_seconds >= 0);
  GPR_DEBUG_ASSERT(ratio_for_successes >= 0);
  GPR_DEBUG_ASSERT(paddings >= 0);
  window_size_ = window_size_seconds == 0 ? window_size_seconds * GPR_MS_PER_SEC
                                          : kDefaultThrottleWindowSize;
  ratio_for_successes_ =
      ratio_for_successes ?: kDefaultThrottleRatioForSuccesses;
  paddings_ = paddings ?: kDefaultThrottlePaddings;
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
  bool result = (gpr_generate_uniform_random_number(&rng_state_) *
                     (requests + paddings_) <
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
    : channel_(std::move(channel)) {
  GRPC_CLOSURE_INIT(&on_ready_locked_cb_, OnReadyLocked,
                    reinterpret_cast<void*>(this), nullptr);
}

void RlsLb::ControlChannel::StateWatcher::OnConnectivityStateChange(
    grpc_connectivity_state new_state) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[RlsLb %p] ControlChannel=%p, StateWatcher=%p, new_state=%d: "
            "connectivity state change",
            channel_->lb_policy_.get(), channel_.get(), this, new_state);
  }
  if (new_state == GRPC_CHANNEL_READY && was_transient_failure_) {
    was_transient_failure_ = false;
    Ref().release();
    channel_->lb_policy_->combiner()->Run(&on_ready_locked_cb_,
                                          GRPC_ERROR_NONE);
  } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    was_transient_failure_ = true;
  }
}

void RlsLb::ControlChannel::StateWatcher::OnReadyLocked(void* arg,
                                                        grpc_error* error) {
  (void)error;
  RefCountedPtr<StateWatcher> watcher =
      RefCountedPtr<StateWatcher>(reinterpret_cast<StateWatcher*>(arg));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[RlsLb %p] ControlChannel=%p, StateWatcher=%p: channel transits "
            "to READY",
            watcher->channel_->lb_policy_.get(), watcher->channel_.get(),
            watcher.get());
  }
  std::lock_guard<std::recursive_mutex> lock(
      watcher->channel_->lb_policy_->mu_);
  if (watcher->channel_->is_shutdown_) return;
  watcher->channel_->lb_policy_->cache_.ResetAllBackoff();
  if (watcher->channel_->lb_policy_->current_config_
          ->request_processing_strategy() ==
      RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR) {
    watcher->channel_->lb_policy_->UpdatePickerLocked();
  }
}

RlsLb::ChildPolicyWrapper* RlsLb::ChildPolicyWrapper::RefHandler::child()
    const {
  return child_.get();
}

// ChildPolicyWrapper implementation
LoadBalancingPolicy::PickResult RlsLb::ChildPolicyWrapper::Pick(PickArgs args) {
  if (picker_ == nullptr) {
    switch (lb_policy_->current_config_->request_processing_strategy()) {
      case RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR:
      case RequestProcessingStrategy::SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR: {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[RlsLb %p] ChildPolicyWrapper=%p: pick queued as the picker "
                  "is not ready",
                  lb_policy_.get(), this);
        }
        PickResult result;
        result.type = PickResult::PICK_QUEUE;
        return result;
      }
      case RequestProcessingStrategy::ASYNC_LOOKUP_DEFAULT_TARGET_ON_MISS: {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
          gpr_log(GPR_DEBUG,
                  "[RlsLb %p] ChildPolicyWrapper=%p: pick queued as the picker "
                  "of the default child policy is not ready",
                  lb_policy_.get(), this);
        }
        // the default child policy is not ready; queue the pick and wait for
        // its picker to be reported
        PickResult result;
        result.type = PickResult::PICK_FAILED;
        result.error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "child policy picker not ready");
        return result;
      }
      default:
        abort();
    }
  } else {
    return picker_->Pick(args);
  }
}

bool RlsLb::ChildPolicyWrapper::IsReady() const { return (picker_ != nullptr); }

void RlsLb::ChildPolicyWrapper::UpdateLocked(
    const Json& child_policy_config, ServerAddressList addresses,
    const grpc_channel_args* channel_args) {
  UpdateArgs update_args;
  grpc_error* error = GRPC_ERROR_NONE;
  update_args.config = LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
      child_policy_config, &error);
  GPR_DEBUG_ASSERT(error == GRPC_ERROR_NONE);
  // returned RLS target fails the validation
  if (error != GRPC_ERROR_NONE) {
    picker_ = std::unique_ptr<LoadBalancingPolicy::SubchannelPicker>(
        new TransientFailurePicker(error));
    child_policy_ = nullptr;
    return;
  }

  Args create_args;
  create_args.combiner = lb_policy_->combiner();
  create_args.channel_control_helper.reset(new ChildPolicyHelper(Ref()));
  create_args.args = channel_args;

  child_policy_.reset(
      new ChildPolicyHandler(std::move(create_args), &grpc_lb_rls_trace));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(
        GPR_INFO,
        "[RlsLb %p] ChildPolicyWrapper=%p, create new child policy handler %p",
        lb_policy_.get(), this, child_policy_.get());
  }
  grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                   lb_policy_->interested_parties());

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

void RlsLb::ChildPolicyWrapper::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[RlsLb %p] ChildPolicyWrapper=%p: child policy wrapper shutdown",
            lb_policy_.get(), this);
  }
  is_shutdown_ = true;
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     lb_policy_->interested_parties());
    child_policy_.reset();
  }
  picker_.reset();
  lb_policy_->child_policy_map_.erase(target_);
}

// ChildPolicyHelper implementation
RefCountedPtr<SubchannelInterface>
RlsLb::ChildPolicyWrapper::ChildPolicyHelper::CreateSubchannel(
    const grpc_channel_args& args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[RlsLb %p] ChildPolicyHelper=%p, ChildPolicyWrapper=%p: "
            "CreateSubchannel",
            wrapper_->lb_policy_.get(), this, wrapper_.get());
  }
  std::lock_guard<std::recursive_mutex> lock(wrapper_->lb_policy_->mu_);
  if (wrapper_->is_shutdown_) return nullptr;
  return wrapper_->lb_policy_->channel_control_helper()->CreateSubchannel(args);
}

void RlsLb::ChildPolicyWrapper::ChildPolicyHelper::UpdateState(
    grpc_connectivity_state state, std::unique_ptr<SubchannelPicker> picker) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[RlsLb %p] ChildPolicyHelper=%p, ChildPolicyWrapper=%p: "
            "UpdateState(state=%d, picker=%p)",
            wrapper_->lb_policy_.get(), this, wrapper_.get(), state,
            picker.get());
  }
  std::lock_guard<std::recursive_mutex> lock(wrapper_->lb_policy_->mu_);
  if (wrapper_->is_shutdown_) return;

  wrapper_->connectivity_state_ = state;
  GPR_DEBUG_ASSERT(picker != nullptr);
  if (picker != nullptr) {
    wrapper_->picker_ = std::move(picker);
  }
  // even if the request processing strategy is async, we still need to update
  // the picker since it's possible that a pick was queued because the default
  // target was not ready
  wrapper_->lb_policy_->UpdatePickerLocked();
}

void RlsLb::ChildPolicyWrapper::ChildPolicyHelper::RequestReresolution() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG,
            "[RlsLb %p] ChildPolicyHelper=%p, ChildPolicyWrapper=%p: "
            "RequestReresolution",
            wrapper_->lb_policy_.get(), this, wrapper_.get());
  }
  std::lock_guard<std::recursive_mutex> lock(wrapper_->lb_policy_->mu_);
  if (wrapper_->is_shutdown_) return;

  wrapper_->lb_policy_->channel_control_helper()->RequestReresolution();
}

void RlsLb::ChildPolicyWrapper::ChildPolicyHelper::AddTraceEvent(
    TraceSeverity severity, StringView message) {
  std::lock_guard<std::recursive_mutex> lock(wrapper_->lb_policy_->mu_);
  if (wrapper_->is_shutdown_) return;

  wrapper_->lb_policy_->channel_control_helper()->AddTraceEvent(severity,
                                                                message);
}

// RlsLb implementation

RlsLb::RlsLb(Args args) : LoadBalancingPolicy(std::move(args)), cache_(this) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[RlsLb %p] policy created", this);
  }
}

const char* RlsLb::name() const { return kRls; }

void RlsLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[RlsLb %p] policy updated", this);
  }
  std::lock_guard<std::recursive_mutex> lock(mu_);

  RefCountedPtr<RlsLbConfig> old_config = current_config_;
  ServerAddressList old_addresses = std::move(current_addresses_);
  current_config_ = args.config;
  current_addresses_ = args.addresses;
  if (current_channel_args_ == nullptr ||
      0 != grpc_channel_args_compare(current_channel_args_, args.args)) {
    if (current_channel_args_ != nullptr) {
      grpc_channel_args_destroy(current_channel_args_);
    }
    current_channel_args_ = grpc_channel_args_copy(args.args);
  }

  const grpc_arg* arg = grpc_channel_args_find(args.args, GRPC_ARG_SERVER_URI);
  const char* server_uri_str = grpc_channel_arg_get_string(arg);
  GPR_ASSERT(server_uri_str != nullptr);
  grpc_uri* uri = grpc_uri_parse(server_uri_str, true);
  GPR_ASSERT(uri->path[0] != '\0');
  server_uri_ = std::string(uri->path[0] == '/' ? uri->path + 1 : uri->path);
  grpc_uri_destroy(uri);

  if (old_config == nullptr ||
      current_config_->lookup_service() != old_config->lookup_service()) {
    channel_ = RefCountedPtr<ControlChannel>(new ControlChannel(
        Ref(), current_config_->lookup_service(), current_channel_args_));
  }

  if (old_config == nullptr ||
      current_config_->cache_size_bytes() != old_config->cache_size_bytes()) {
    if (current_config_->cache_size_bytes() != 0) {
      cache_.Resize(current_config_->cache_size_bytes());
    } else {
      cache_.Resize(kDefaultCacheSizeBytes);
    }
  }

  bool default_child_policy_updated = false;
  if (current_config_->request_processing_strategy() !=
          RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR &&
      (old_config == nullptr ||
       current_config_->default_target() != old_config->default_target())) {
    default_child_policy_.reset(new ChildPolicyWrapper::RefHandler(
        new ChildPolicyWrapper(Ref(), current_config_->default_target())));
    default_child_policy_->child()->UpdateLocked(
        current_config_->child_policy_config(), current_addresses_,
        current_channel_args_);
    default_child_policy_updated = true;
  }

  if (old_config == nullptr ||
      (current_config_->child_policy_config() !=
       old_config->child_policy_config()) ||
      (current_addresses_ != old_addresses)) {
    if (current_config_->request_processing_strategy() !=
            RequestProcessingStrategy::SYNC_LOOKUP_CLIENT_SEES_ERROR &&
        !default_child_policy_updated) {
      default_child_policy_->child()->UpdateLocked(
          current_config_->child_policy_config(), current_addresses_,
          current_channel_args_);
    }
    for (auto& child : child_policy_map_) {
      Json copied_child_policy_config = current_config_->child_policy_config();
      grpc_error* error = InsertOrUpdateChildPolicyField(
          &copied_child_policy_config,
          current_config_->child_policy_config_target_field_name(),
          child.second->child()->target());
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      child.second->child()->UpdateLocked(copied_child_policy_config,
                                          current_addresses_,
                                          current_channel_args_);
    }
  }

  if (old_config == nullptr || current_config_->request_processing_strategy() !=
                                   old_config->request_processing_strategy()) {
    UpdatePickerLocked();
  }
}

void RlsLb::ExitIdleLocked() {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  for (auto& child_entry : child_policy_map_) {
    child_entry.second->child()->ExitIdleLocked();
  }
}

void RlsLb::ResetBackoffLocked() {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  channel_->ResetBackoff();
  cache_.ResetAllBackoff();
  for (auto& child : child_policy_map_) {
    child.second->child()->ResetBackoffLocked();
  }
}

void RlsLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[RlsLb %p] policy shutdown", this);
  }
  std::lock_guard<std::recursive_mutex> lock(mu_);
  is_shutdown_ = true;
  current_config_.reset();
  if (current_channel_args_ != nullptr) {
    grpc_channel_args_destroy(current_channel_args_);
  }
  cache_.Shutdown();
  request_map_.clear();
  channel_.reset();
  default_child_policy_.reset();
}

const RlsLb::KeyMapBuilder* RlsLb::FindKeyMapBuilder(
    const std::string& path) const {
  return RlsFindKeyMapBuilder(current_config_->key_map_builder_map(), path);
}

bool RlsLb::MaybeMakeRlsCall(const Key& key,
                             std::unique_ptr<BackOff>* backoff_state) {
  auto it = request_map_.find(key);
  if (it == request_map_.end()) {
    if (channel_->ShouldThrottle()) {
      return false;
    }
    request_map_.emplace(
        key,
        MakeOrphanable<RequestMapEntry>(
            Ref(), key, channel_,
            backoff_state == nullptr ? nullptr : std::move(*backoff_state)));
  }
  return true;
}

void RlsLb::UpdatePickerLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_rls_trace)) {
    gpr_log(GPR_DEBUG, "[RlsLb %p] update picker", this);
  }
  // TODO(mxyan): more sophisticated channel state inference?
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_READY, std::unique_ptr<Picker>(new Picker(Ref())));
}

RlsLb::KeyMapBuilder::KeyMapBuilder(const Json& config, grpc_error** error) {
  *error = GRPC_ERROR_NONE;
  if (config.type() == Json::Type::JSON_NULL) return;
  if (config.type() != Json::Type::ARRAY) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"headers\" field is not an array");
    return;
  }
  InlinedVector<grpc_error*, 1> error_list;
  grpc_error* internal_error = GRPC_ERROR_NONE;
  auto headers = config.array_value();
  pattern_.reserve(headers.size());
  std::unordered_set<std::string> key_set;
  key_set.reserve(headers.size());

  int idx = 0;
  for (auto& name_matcher_json : headers) {
    if (name_matcher_json.type() != Json::Type::OBJECT) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("\"headers\" array element ", idx, " is not an object")
              .c_str()));
    }
    auto& name_matcher = name_matcher_json.object_value();
    auto required_match_json = name_matcher.find("required_match");
    if (required_match_json != name_matcher.end()) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"required_match\" field should not be set"));
    }
    auto key_ptr =
        ParseStringFieldFromJsonObject(name_matcher, "key", &internal_error);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else {
      const std::string& key = *key_ptr;
      if (key_set.find(key) != key_set.end()) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrCat("duplicate key \"", key, "\"").c_str()));
      } else {
        key_set.insert(key);
        auto names_ptr = ParseArrayFieldFromJsonObject(name_matcher, "names",
                                                       &internal_error, true);
        if (internal_error != GRPC_ERROR_NONE) {
          error_list.push_back(internal_error);
        } else if (names_ptr != nullptr) {
          auto& names = *names_ptr;
          int idx2 = 0;
          for (auto& name_json : names) {
            if (name_json.type() != Json::Type::STRING) {
              error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                  absl::StrCat("\"names\" array element ,", idx2,
                               " is not a string")
                      .c_str()));
            } else {
              auto& name = name_json.string_value();
              // Use the index of the element as the key's priority.
              pattern_[name].push_back(std::make_pair(key, idx2));
            }
            idx2++;
          }
        }
      }
    }
    idx++;
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR(
      "errors parsing RLS key builder config", &error_list);
}

RlsLb::KeyMap RlsLb::KeyMapBuilder::BuildKeyMap(
    const MetadataInterface* initial_metadata) const {
  if (initial_metadata == nullptr) return KeyMap();

  KeyMap key_map;
  key_map.reserve(pattern_.size());
  std::unordered_map<std::string, int> priority_map;

  for (auto it = initial_metadata->begin(); it != initial_metadata->end();
       ++it) {
    auto md_field = (*it).first;
    auto md_value = (*it).second;

    auto key_list_it = pattern_.find(std::string(md_field));
    if (key_list_it != pattern_.end()) {
      auto& key_list = key_list_it->second;

      for (auto& key_priority_pair : key_list) {
        auto& key = key_priority_pair.first;
        auto& priority = key_priority_pair.second;

        auto key_map_entry_it = key_map.find(key);
        if (key_map_entry_it == key_map.end() || priority < priority_map[key]) {
          key_map[key] = std::string(md_value);
          priority_map[key] = priority;
        } else if (key_map_entry_it != key_map.end() &&
                   priority == priority_map[key]) {
          key_map[key] += ',';
          key_map[key] += std::string(md_value);
        }
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

RefCountedPtr<LoadBalancingPolicy::Config>
RlsLbFactory::ParseLoadBalancingConfig(const Json& config_json,
                                       grpc_error** error) const {
  *error = GRPC_ERROR_NONE;
  GPR_DEBUG_ASSERT(error != nullptr);
  if (config_json.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("RLS config is not object");
    return nullptr;
  }
  InlinedVector<grpc_error*, 1> error_list;
  RefCountedPtr<RlsLbConfig> result;
  result.reset(new RlsLbConfig());
  grpc_error* internal_error = GRPC_ERROR_NONE;
  auto& config = config_json.object_value();

  auto route_lookup_config_ptr = ParseObjectFieldFromJsonObject(
      config, "routeLookupConfig", &internal_error);
  if (internal_error != GRPC_ERROR_NONE) {
    error_list.push_back(internal_error);
  } else {
    // key_map_builder_map_
    auto grpc_key_builders_json_ptr = ParseFieldJsonFromJsonObject(
        *route_lookup_config_ptr, "grpcKeybuilders", &internal_error, true);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else if (grpc_key_builders_json_ptr != nullptr) {
      result->key_map_builder_map_ = RlsCreateKeyMapBuilderMap(
          *grpc_key_builders_json_ptr, &internal_error);
      if (internal_error != GRPC_ERROR_NONE) {
        error_list.push_back(internal_error);
      }
    }

    // lookup_service
    auto lookup_service = ParseStringFieldFromJsonObject(
        *route_lookup_config_ptr, "lookupService", &internal_error);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else {
      result->lookup_service_ = *lookup_service;
    }

    // lookup_service_timeout
    auto lookup_service_timeout_ptr = ParseObjectFieldFromJsonObject(
        *route_lookup_config_ptr, "lookupServiceTimeout", &internal_error,
        true);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else if (lookup_service_timeout_ptr == nullptr) {
      result->lookup_service_timeout_ = kDefaultLookupServiceTimeout;
    } else {
      result->lookup_service_timeout_ =
          ParseDuration(*lookup_service_timeout_ptr, &internal_error);
      if (internal_error != GRPC_ERROR_NONE) {
        error_list.push_back(internal_error);
      }
    }

    bool max_age_missing = false;
    // max_age
    auto max_age_ptr = ParseObjectFieldFromJsonObject(
        *route_lookup_config_ptr, "maxAge", &internal_error, true);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else if (max_age_ptr == nullptr) {
      result->max_age_ = kDefaultLookupServiceTimeout;
      max_age_missing = true;
    } else {
      result->max_age_ = ParseDuration(*max_age_ptr, &internal_error);
      if (internal_error != GRPC_ERROR_NONE) {
        error_list.push_back(internal_error);
      } else if (result->max_age_ > kMaxMaxAge) {
        result->max_age_ = kMaxMaxAge;
      }
    }

    // stale_age
    auto stale_age_ptr = ParseObjectFieldFromJsonObject(
        *route_lookup_config_ptr, "staleAge", &internal_error, true);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else if (stale_age_ptr == nullptr && max_age_missing) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "max_age needs to be set when stale_age is set"));
    } else if (stale_age_ptr == nullptr && !max_age_missing) {
      result->stale_age_ = result->max_age_;
    } else {
      result->stale_age_ = ParseDuration(*stale_age_ptr, &internal_error);
      if (internal_error != GRPC_ERROR_NONE) {
        error_list.push_back(internal_error);
      } else if (result->stale_age_ > result->max_age_) {
        result->stale_age_ = result->max_age_;
      }
    }

    // cache_size_bytes
    result->cache_size_bytes_ = ParseNumberFieldFromJsonObject(
        *route_lookup_config_ptr, "cacheSizeBytes", &internal_error, true,
        kDefaultCacheSizeBytes);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else if (result->cache_size_bytes_ <= 0) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "cache_size_bytes must be greater than 0"));
    }

    // request processing strategy
    result->request_processing_strategy_ =
        static_cast<RlsLb::RequestProcessingStrategy>(
            ParseNumberFieldFromJsonObject(*route_lookup_config_ptr,
                                           "requestProcessingStrategy",
                                           &internal_error));
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else if (static_cast<int>(result->request_processing_strategy_) < 0 ||
               result->request_processing_strategy_ >=
                   RlsLb::RequestProcessingStrategy::STRATEGY_UNSPECIFIED) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"request_processing_strategy\" field invalid"));
    }

    // default target
    auto default_target_ptr = ParseStringFieldFromJsonObject(
        *route_lookup_config_ptr, "defaultTarget", &internal_error, true);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else if ((default_target_ptr == nullptr ||
                default_target_ptr->length() == 0) &&
               (result->request_processing_strategy_ ==
                    RlsLb::RequestProcessingStrategy::
                        SYNC_LOOKUP_DEFAULT_TARGET_ON_ERROR ||
                result->request_processing_strategy_ ==
                    RlsLb::RequestProcessingStrategy::
                        ASYNC_LOOKUP_DEFAULT_TARGET_ON_MISS)) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "selected request processing strategy needs default_target"));
    } else if (default_target_ptr != nullptr &&
               default_target_ptr->length() > 0) {
      result->default_target_ = *default_target_ptr;
    }
  }

  // child_policy_config_target_field_name
  auto child_policy_config_target_field_name_ptr =
      ParseStringFieldFromJsonObject(config, "childPolicyConfigTargetFieldName",
                                     &internal_error);
  if (internal_error != GRPC_ERROR_NONE) {
    error_list.push_back(internal_error);
  } else if (child_policy_config_target_field_name_ptr->length() == 0) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "child policy config target field name is empty"));
  } else {
    result->child_policy_config_target_field_name_ =
        *child_policy_config_target_field_name_ptr;
    auto child_policy_array_json_ptr =
        ParseFieldJsonFromJsonObject(config, "childPolicy", &internal_error);
    if (internal_error != GRPC_ERROR_NONE) {
      error_list.push_back(internal_error);
    } else if (child_policy_array_json_ptr->type() != Json::Type::ARRAY) {
      error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "\"childPolicy\" field is not an array"));
    } else {
      // Fill in the child_policy_config_target_field_name field with default
      // target for all the child policy config and validate them
      Json new_child_policy_array_json = *child_policy_array_json_ptr;
      auto new_child_policy_array = new_child_policy_array_json.mutable_array();
      for (auto& child_policy_json : *new_child_policy_array) {
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
          auto child_policy = child_policy_json.mutable_object();
          auto it = child_policy->begin();
          auto& child_policy_config_json = it->second;
          if (child_policy_config_json.type() != Json::Type::OBJECT) {
            error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(
                absl::StrCat("child policy configuration of \"", it->first,
                             "\" is not object")
                    .c_str()));
            continue;
          }
          auto child_policy_config = child_policy_config_json.mutable_object();
          (*child_policy_config)[result
                                     ->child_policy_config_target_field_name_] =
              result->request_processing_strategy_ ==
                      RlsLb::RequestProcessingStrategy::
                          SYNC_LOOKUP_CLIENT_SEES_ERROR
                  ? kDummyTargetFieldValue
                  : result->default_target_;
        }
      }
      result->default_child_policy_parsed_config_ =
          LoadBalancingPolicyRegistry::ParseLoadBalancingConfig(
              new_child_policy_array_json, &internal_error);
      if (internal_error != GRPC_ERROR_NONE) {
        error_list.push_back(internal_error);
      } else {
        for (auto& child_policy_json : *new_child_policy_array) {
          if (child_policy_json.type() == Json::Type::OBJECT &&
              child_policy_json.object_value().size() == 1 &&
              child_policy_json.object_value().begin()->first ==
                  result->default_child_policy_parsed_config_->name()) {
            Json selected_child_policy_config = std::move(child_policy_json);
            new_child_policy_array->clear();
            new_child_policy_array->push_back(
                std::move(selected_child_policy_config));
            // Intentionally left the default target in the child policy config
            // for easier processing in UpdateLocked().
            result->child_policy_config_ = std::move(*new_child_policy_array);
            break;
          }
        }
      }
    }
  }

  *error =
      GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing RLS config", &error_list);
  return result;
}

}  // namespace grpc_core

void grpc_lb_policy_rls_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::RlsLbFactory>());
}

void grpc_lb_policy_rls_shutdown() {}
