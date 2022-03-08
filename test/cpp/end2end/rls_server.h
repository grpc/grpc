//
// Copyright 2022 gRPC authors.
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

#include "src/core/lib/gprpp/time.h"
#include "src/proto/grpc/lookup/v1/rls.grpc.pb.h"
#include "src/proto/grpc/lookup/v1/rls.pb.h"
#include "test/cpp/end2end/counted_service.h"

using ::grpc::lookup::v1::RouteLookupRequest;
using ::grpc::lookup::v1::RouteLookupResponse;

namespace grpc {
namespace testing {

using RlsService =
    CountedService<::grpc::lookup::v1::RouteLookupService::Service>;

class RlsServiceImpl : public RlsService {
 public:
  grpc::Status RouteLookup(grpc::ServerContext* context,
                           const RouteLookupRequest* request,
                           RouteLookupResponse* response) override;

  void Start() {}

  void Shutdown() {}

  void SetResponse(RouteLookupRequest request, RouteLookupResponse response,
                   grpc_core::Duration response_delay = grpc_core::Duration());

  void RemoveResponse(const RouteLookupRequest& request);

  std::vector<RouteLookupRequest> GetUnmatchedRequests();

 private:
  // Sorting thunk for RouteLookupRequest.
  struct RlsRequestLessThan {
    bool operator()(const RouteLookupRequest& req1,
                    const RouteLookupRequest& req2) const {
      std::map<absl::string_view, absl::string_view> key_map1(
          req1.key_map().begin(), req1.key_map().end());
      std::map<absl::string_view, absl::string_view> key_map2(
          req2.key_map().begin(), req2.key_map().end());
      if (key_map1 < key_map2) return true;
      if (req1.reason() < req2.reason()) return true;
      if (req1.stale_header_data() < req2.stale_header_data()) return true;
      return false;
    }
  };

  struct ResponseData {
    RouteLookupResponse response;
    grpc_core::Duration response_delay;
  };

  grpc::internal::Mutex mu_;
  std::map<RouteLookupRequest, ResponseData, RlsRequestLessThan> responses_
      ABSL_GUARDED_BY(&mu_);
  std::vector<RouteLookupRequest> unmatched_requests_ ABSL_GUARDED_BY(&mu_);
};

static RouteLookupRequest BuildRlsRequest(
    std::map<std::string, std::string> key,
    RouteLookupRequest::Reason reason = RouteLookupRequest::REASON_MISS,
    const char* stale_header_data = "") {
  RouteLookupRequest request;
  request.set_target_type("grpc");
  request.mutable_key_map()->insert(key.begin(), key.end());
  request.set_reason(reason);
  request.set_stale_header_data(stale_header_data);
  return request;
}

static RouteLookupResponse BuildRlsResponse(std::vector<std::string> targets,
                                            const char* header_data = "") {
  RouteLookupResponse response;
  response.mutable_targets()->Add(targets.begin(), targets.end());
  response.set_header_data(header_data);
  return response;
}
}  // namespace testing
}  // namespace grpc
