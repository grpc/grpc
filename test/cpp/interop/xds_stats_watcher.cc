// Copyright 2023 gRPC authors.
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

#include "test/cpp/interop/xds_stats_watcher.h"

#include <map>

namespace grpc {
namespace testing {

namespace {

LoadBalancerStatsResponse::RpcMetadata BuildRpcMetadata(
    absl::Span<const std::string> metadata_keys,
    const std::multimap<grpc::string_ref, grpc::string_ref>& initial_metadata) {
  LoadBalancerStatsResponse::RpcMetadata rpc_metadata;
  for (const auto& key : metadata_keys) {
    auto matching = initial_metadata.equal_range(key);
    for (auto value = matching.first; value != matching.second; ++value) {
      auto entry = rpc_metadata.add_metadata();
      entry->set_key(key);
      entry->set_value(
          absl::string_view(value->second.data(), value->second.length()));
    }
  }
  return rpc_metadata;
}

}  // namespace

XdsStatsWatcher::XdsStatsWatcher(int start_id, int end_id,
                                 absl::Span<const std::string> metadata_keys)
    : start_id_(start_id),
      end_id_(end_id),
      rpcs_needed_(end_id - start_id),
      metadata_keys_(metadata_keys.begin(), metadata_keys.end()) {}

void XdsStatsWatcher::RpcCompleted(
    const AsyncClientCallResult& call, const std::string& peer,
    const std::multimap<grpc::string_ref, grpc::string_ref>& initial_metadata) {
  // We count RPCs for global watcher or if the request_id falls into the
  // watcher's interested range of request ids.
  if ((start_id_ == 0 && end_id_ == 0) ||
      (start_id_ <= call.saved_request_id && call.saved_request_id < end_id_)) {
    {
      std::lock_guard<std::mutex> lock(m_);
      if (peer.empty()) {
        no_remote_peer_++;
        ++no_remote_peer_by_type_[call.rpc_type];
      } else {
        // RPC is counted into both per-peer bin and per-method-per-peer bin.
        rpcs_by_peer_[peer]++;
        rpcs_by_type_[call.rpc_type][peer]++;
        *metadata_by_peer_[peer].add_rpc_metadata() =
            BuildRpcMetadata(metadata_keys_, initial_metadata);
      }
      rpcs_needed_--;
      // Report accumulated stats.
      auto& stats_per_method = *accumulated_stats_.mutable_stats_per_method();
      auto& method_stat =
          stats_per_method[ClientConfigureRequest_RpcType_Name(call.rpc_type)];
      auto& result = *method_stat.mutable_result();
      grpc_status_code code =
          static_cast<grpc_status_code>(call.status.error_code());
      auto& num_rpcs = result[code];
      ++num_rpcs;
      auto rpcs_started = method_stat.rpcs_started();
      method_stat.set_rpcs_started(++rpcs_started);
    }
    cv_.notify_one();
  }
}

LoadBalancerStatsResponse XdsStatsWatcher::WaitForRpcStatsResponse(
    int timeout_sec) {
  LoadBalancerStatsResponse response;
  std::unique_lock<std::mutex> lock(m_);
  cv_.wait_for(lock, std::chrono::seconds(timeout_sec),
               [this] { return rpcs_needed_ == 0; });
  response.mutable_rpcs_by_peer()->insert(rpcs_by_peer_.begin(),
                                          rpcs_by_peer_.end());
  response.mutable_metadatas_by_peer()->insert(metadata_by_peer_.begin(),
                                               metadata_by_peer_.end());
  auto& response_rpcs_by_method = *response.mutable_rpcs_by_method();
  for (const auto& rpc_by_type : rpcs_by_type_) {
    std::string method_name;
    if (rpc_by_type.first == ClientConfigureRequest::EMPTY_CALL) {
      method_name = "EmptyCall";
    } else if (rpc_by_type.first == ClientConfigureRequest::UNARY_CALL) {
      method_name = "UnaryCall";
    } else {
      GPR_ASSERT(0);
    }
    // TODO(@donnadionne): When the test runner changes to accept EMPTY_CALL
    // and UNARY_CALL we will just use the name of the enum instead of the
    // method_name variable.
    auto& response_rpc_by_method = response_rpcs_by_method[method_name];
    auto& response_rpcs_by_peer =
        *response_rpc_by_method.mutable_rpcs_by_peer();
    for (const auto& rpc_by_peer : rpc_by_type.second) {
      auto& response_rpc_by_peer = response_rpcs_by_peer[rpc_by_peer.first];
      response_rpc_by_peer = rpc_by_peer.second;
    }
  }
  response.set_num_failures(no_remote_peer_ + rpcs_needed_);
  return response;
}

void XdsStatsWatcher::GetCurrentRpcStats(
    LoadBalancerAccumulatedStatsResponse* response,
    StatsWatchers* stats_watchers) {
  std::unique_lock<std::mutex> lock(m_);
  response->CopyFrom(accumulated_stats_);
  // TODO(@donnadionne): delete deprecated stats below when the test is no
  // longer using them.
  auto& response_rpcs_started_by_method =
      *response->mutable_num_rpcs_started_by_method();
  auto& response_rpcs_succeeded_by_method =
      *response->mutable_num_rpcs_succeeded_by_method();
  auto& response_rpcs_failed_by_method =
      *response->mutable_num_rpcs_failed_by_method();
  for (const auto& rpc_by_type : rpcs_by_type_) {
    auto total_succeeded = 0;
    for (const auto& rpc_by_peer : rpc_by_type.second) {
      total_succeeded += rpc_by_peer.second;
    }
    response_rpcs_succeeded_by_method[ClientConfigureRequest_RpcType_Name(
        rpc_by_type.first)] = total_succeeded;
    response_rpcs_started_by_method[ClientConfigureRequest_RpcType_Name(
        rpc_by_type.first)] =
        stats_watchers->global_request_id_by_type[rpc_by_type.first];
    response_rpcs_failed_by_method[ClientConfigureRequest_RpcType_Name(
        rpc_by_type.first)] = no_remote_peer_by_type_[rpc_by_type.first];
  }
}

}  // namespace testing
}  // namespace grpc
