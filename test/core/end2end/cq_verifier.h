//
//
// Copyright 2015 gRPC authors.
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
//

#ifndef GRPC_TEST_CORE_END2END_CQ_VERIFIER_H
#define GRPC_TEST_CORE_END2END_CQ_VERIFIER_H

#include <string>
#include <vector>

#include "absl/types/variant.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>

#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_core {

// A CqVerifier can verify that expected events arrive in a timely fashion
// on a single completion queue
class CqVerifier {
 public:
  // ExpectedResult - if the tag is received, set *seen to true (if seen is
  // non-null).
  struct Maybe {
    bool* seen = nullptr;
  };
  // ExpectedResult - expect the tag, but its result may be true or false.
  // Store the result in result (if result is non-null).
  struct AnyStatus {
    bool* result = nullptr;
  };

  using ExpectedResult = absl::variant<bool, Maybe, AnyStatus>;

  explicit CqVerifier(grpc_completion_queue* cq);
  ~CqVerifier();

  CqVerifier(const CqVerifier&) = delete;
  CqVerifier& operator=(const CqVerifier&) = delete;

  // Ensure all expected events (and only those events) are present on the
  // bound completion queue within \a timeout.
  void Verify(Duration timeout = Duration::Seconds(10),
              SourceLocation location = SourceLocation());

  // Ensure that the completion queue is empty, waiting up to \a timeout.
  void VerifyEmpty(Duration timeout = Duration::Seconds(1),
                   SourceLocation location = SourceLocation());

  // Match an expectation about a status.
  // location must be DEBUG_LOCATION.
  // result can be any of the types in ExpectedResult - a plain bool means
  // 'expect success to be true/false'.
  void Expect(void* tag, ExpectedResult result,
              SourceLocation location = SourceLocation());

  std::string ToString() const;

 private:
  struct Expectation {
    SourceLocation location;
    void* tag;
    ExpectedResult result;

    std::string ToString() const;
  };

  void FailNoEventReceived(const SourceLocation& location) const;
  void FailUnexpectedEvent(grpc_event* ev,
                           const SourceLocation& location) const;
  bool AllMaybes() const;

  grpc_completion_queue* const cq_;
  std::vector<Expectation> expectations_;
};

}  // namespace grpc_core

int byte_buffer_eq_slice(grpc_byte_buffer* bb, grpc_slice b);
int byte_buffer_eq_string(grpc_byte_buffer* bb, const char* str);
int contains_metadata(grpc_metadata_array* array, const char* key,
                      const char* value);
int contains_metadata_slices(grpc_metadata_array* array, grpc_slice key,
                             grpc_slice value);

#endif  // GRPC_TEST_CORE_END2END_CQ_VERIFIER_H
