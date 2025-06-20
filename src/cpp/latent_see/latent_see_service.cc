// Copyright 2025 gRPC authors.
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

#include "src/cpp/latent_see/latent_see_service.h"

#include "src/core/util/latent_see.h"

namespace grpc {

namespace {

class StreamingOutput final : public grpc_core::latent_see::Output {
 public:
  explicit StreamingOutput(ServerWriter<channelz::v2::LatentSeeTrace>* response)
      : response_(response) {}

  void Mark(absl::string_view name, int64_t tid, int64_t timestamp) override {
    channelz::v2::LatentSeeTrace trace;
    trace.set_name(name);
    trace.set_tid(tid);
    trace.set_timestamp(timestamp);
    trace.mutable_mark();
    response_->Write(trace);
  }
  void FlowBegin(absl::string_view name, int64_t tid, int64_t timestamp,
                 int64_t flow_id) override {
    channelz::v2::LatentSeeTrace trace;
    trace.set_name(name);
    trace.set_tid(tid);
    trace.set_timestamp(timestamp);
    trace.mutable_flow_begin()->set_id(flow_id);
    response_->Write(trace);
  }
  void FlowEnd(absl::string_view name, int64_t tid, int64_t timestamp,
               int64_t flow_id) override {
    channelz::v2::LatentSeeTrace trace;
    trace.set_name(name);
    trace.set_tid(tid);
    trace.set_timestamp(timestamp);
    trace.mutable_flow_end()->set_id(flow_id);
    response_->Write(trace);
  }
  void Span(absl::string_view name, int64_t tid, int64_t timestamp_begin,
            int64_t duration) override {
    channelz::v2::LatentSeeTrace trace;
    trace.set_name(name);
    trace.set_tid(tid);
    trace.set_timestamp(timestamp_begin);
    trace.mutable_span()->set_duration(duration);
    response_->Write(trace);
  }
  void Finish() override {}

 private:
  ServerWriter<channelz::v2::LatentSeeTrace>* response_;
};

}  // namespace

Status LatentSeeService::GetTrace(
    ServerContext*, const channelz::v2::GetTraceRequest* request,
    ServerWriter<channelz::v2::LatentSeeTrace>* response) {
  StreamingOutput output(response);
  grpc_core::latent_see::Collect(
      nullptr,
      absl::Seconds(std::min(options_.max_query_time, request->sample_time())),
      options_.max_memory, &output);
  return Status::OK;
}

}  // namespace grpc
