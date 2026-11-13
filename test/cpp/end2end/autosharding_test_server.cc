//
// Copyright 2026 gRPC authors.
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

#include "test/cpp/end2end/autosharding_test_server.h"

#include <thread>

#include "absl/log/log.h"

namespace grpc {
namespace testing {

Status AutoshardingServiceImpl::WatchShardingAssignment(
    ServerContext* context, ServerReaderWriter<Response, Request>* stream) {
  IncreaseRequestCount();
  // The first request is the InitialClientConfig.  Record it before sending
  // any assignment.
  Request initial_request;
  if (!stream->Read(&initial_request)) return Status::OK;
  LOG(INFO) << "autosharding server: received initial request: "
            << initial_request.ShortDebugString();
  const std::string& target = initial_request.initial_client_config().target();
  bool silent;
  {
    grpc_core::MutexLock lock(&mu_);
    targets_.push_back(target);
    silent = target == silent_target_;
  }
  // Read the later requests (ACKs) in their own thread, so that reads do not
  // block writes.
  std::thread reader([stream]() {
    Request request;
    while (stream->Read(&request)) {
      LOG(INFO) << "autosharding server: received request: "
                << request.ShortDebugString();
    }
  });
  uint64_t sent_version = 0;
  bool stream_ok = true;
  while (stream_ok) {
    std::vector<Response> responses;
    {
      grpc_core::MutexLock lock(&mu_);
      while (!shutdown_ && (silent || sent_version == assignment_version_)) {
        cond_.Wait(&mu_);
      }
      if (shutdown_) break;
      responses = assignment_;
      sent_version = assignment_version_;
    }
    for (const Response& response : responses) {
      stream_ok = stream->Write(response);
      if (!stream_ok) break;
    }
  }
  // Cancel the call, so that the reader thread returns.
  context->TryCancel();
  reader.join();
  return Status::OK;
}

void AutoshardingServiceImpl::Start() {
  grpc_core::MutexLock lock(&mu_);
  shutdown_ = false;
}

void AutoshardingServiceImpl::Shutdown() {
  grpc_core::MutexLock lock(&mu_);
  shutdown_ = true;
  cond_.SignalAll();
}

void AutoshardingServiceImpl::SetAssignment(
    const std::vector<Slice>& slices,
    const std::vector<std::string>& endpoint_names, int64_t generation) {
  Response chunk_response;
  auto* chunk = chunk_response.mutable_chunk();
  absl::string_view start_key;
  for (const Slice& slice : slices) {
    auto* slice_assignment = chunk->add_slice_assignments();
    slice_assignment->mutable_slice()->set_start_key(start_key);
    if (!slice.end_key.empty()) {
      slice_assignment->mutable_slice()->set_end_key(slice.end_key);
    }
    for (int32_t index : slice.endpoint_indices) {
      slice_assignment->add_endpoints()->set_endpoint_index(index);
    }
    start_key = slice.end_key;
  }
  for (const std::string& name : endpoint_names) {
    chunk->add_endpoints()->set_endpoint(name);
  }
  Response metadata_response;
  metadata_response.mutable_metadata()->set_generation(generation);
  grpc_core::MutexLock lock(&mu_);
  assignment_ = {std::move(chunk_response), std::move(metadata_response)};
  ++assignment_version_;
  cond_.SignalAll();
}

void AutoshardingServiceImpl::SetSilentTarget(std::string target) {
  grpc_core::MutexLock lock(&mu_);
  silent_target_ = std::move(target);
}

std::vector<std::string> AutoshardingServiceImpl::targets() {
  grpc_core::MutexLock lock(&mu_);
  return targets_;
}

}  // namespace testing
}  // namespace grpc
