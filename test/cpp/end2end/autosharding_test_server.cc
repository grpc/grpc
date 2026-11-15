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

#include "absl/log/log.h"

namespace grpc {
namespace testing {

//
// AutoshardingServiceImpl::Reactor
//

// One WatchShardingAssignment stream.  Reads and writes are driven by the
// gRPC callback API, so no per-RPC thread is needed (gRFC A119).
class AutoshardingServiceImpl::Reactor final
    : public grpc::ServerBidiReactor<Request, Response> {
 public:
  explicit Reactor(AutoshardingServiceImpl* service) : service_(service) {
    service_->IncreaseRequestCount();
    grpc_core::MutexLock lock(service_->mu_);
    if (service_->shutdown_) {
      MaybeFinishLocked(grpc::Status(grpc::StatusCode::UNAVAILABLE,
                                     "server shutdown"));
      return;
    }
    service_->reactors_.insert(this);
    StartRead(&request_);
  }

  void OnReadDone(bool ok) override {
    grpc_core::MutexLock lock(service_->mu_);
    if (called_finish_) return;
    if (!ok) {
      MaybeFinishLocked(grpc::Status::OK);
      return;
    }
    if (first_read_) {
      first_read_ = false;
      const std::string target = request_.initial_client_config().target();
      service_->targets_.push_back(target);
      silent_ = (target == service_->silent_target_);
      LOG(INFO) << "autosharding server: received initial request: "
                << request_.ShortDebugString();
    } else {
      LOG(INFO) << "autosharding server: received request: "
                << request_.ShortDebugString();
    }
    if (!service_->shutdown_) {
      StartRead(&request_);
    }
    SendAssignmentLocked();
  }

  void OnWriteDone(bool ok) override {
    grpc_core::MutexLock lock(service_->mu_);
    if (called_finish_) return;
    if (!ok) {
      MaybeFinishLocked(grpc::Status::OK);
      return;
    }
    ++next_write_index_;
    if (next_write_index_ < pending_assignment_.size()) {
      StartWrite(&pending_assignment_[next_write_index_]);
    } else {
      writing_ = false;
      SendAssignmentLocked();
    }
  }

  void OnDone() override {
    {
      grpc_core::MutexLock lock(service_->mu_);
      service_->reactors_.erase(this);
    }
    delete this;
  }

  void OnCancel() override {
    grpc_core::MutexLock lock(service_->mu_);
    MaybeFinishLocked(grpc::Status::CANCELLED);
  }

  // Finishes the stream, unless it is already finished.  service_->mu_ must
  // be held.
  void MaybeFinishLocked(const grpc::Status& status) {
    if (called_finish_) return;
    called_finish_ = true;
    Finish(status);
  }

  // Starts sending the latest assignment, if one is available and no write is
  // in flight.  service_->mu_ must be held.
  void SendAssignmentLocked() {
    if (called_finish_ || writing_ || silent_ || service_->shutdown_) return;
    if (service_->assignment_version_ == sent_version_) return;
    sent_version_ = service_->assignment_version_;
    pending_assignment_ = service_->assignment_;
    next_write_index_ = 0;
    writing_ = true;
    StartWrite(&pending_assignment_[0]);
  }

 private:
  AutoshardingServiceImpl* const service_;
  Request request_;
  std::vector<Response> pending_assignment_;
  size_t next_write_index_ = 0;
  uint64_t sent_version_ = 0;
  bool first_read_ = true;
  bool silent_ = false;
  bool writing_ = false;
  bool called_finish_ = false;
};

//
// AutoshardingServiceImpl
//

grpc::ServerBidiReactor<AutoshardingServiceImpl::Request,
                        AutoshardingServiceImpl::Response>*
AutoshardingServiceImpl::WatchShardingAssignment(
    grpc::CallbackServerContext* /*context*/) {
  return new Reactor(this);
}

void AutoshardingServiceImpl::Start() {
  grpc_core::MutexLock lock(mu_);
  shutdown_ = false;
}

void AutoshardingServiceImpl::Shutdown() {
  grpc_core::MutexLock lock(mu_);
  shutdown_ = true;
  for (Reactor* reactor : reactors_) {
    reactor->MaybeFinishLocked(grpc::Status::CANCELLED);
  }
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
  grpc_core::MutexLock lock(mu_);
  assignment_ = {std::move(chunk_response), std::move(metadata_response)};
  ++assignment_version_;
  for (Reactor* reactor : reactors_) {
    reactor->SendAssignmentLocked();
  }
}

void AutoshardingServiceImpl::SetSilentTarget(std::string target) {
  grpc_core::MutexLock lock(mu_);
  silent_target_ = std::move(target);
}

std::vector<std::string> AutoshardingServiceImpl::targets() {
  grpc_core::MutexLock lock(mu_);
  return targets_;
}

}  // namespace testing
}  // namespace grpc
