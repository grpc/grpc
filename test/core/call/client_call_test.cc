// Copyright 2024 gRPC authors.
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

#include "src/core/lib/surface/client_call.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>

#include "src/core/lib/resource_quota/arena.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

namespace {
const absl::string_view kDefaultPath = "/foo/bar";
}

class ClientCallTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  class CallOptions {
   public:
    Slice path() const { return path_.Copy(); }
    absl::optional<Slice> authority() const {
      return authority_.has_value() ? absl::optional<Slice>(authority_->Copy())
                                    : absl::nullopt;
    }
    bool registered_method() const { return registered_method_; }
    Duration timeout() const { return timeout_; }
    grpc_compression_options compression_options() const {
      return compression_options_;
    }

   private:
    Slice path_ = Slice::FromCopiedString(kDefaultPath);
    absl::optional<Slice> authority_;
    bool registered_method_ = false;
    Duration timeout_ = Duration::Infinity();
    grpc_compression_options compression_options_ = {
        1,
    };
  };

  grpc_call* InitCall(const CallOptions& options) {
    CHECK_EQ(call_, nullptr);
    call_ = MakeClientCall(nullptr, 0, cq_, options.path(), options.authority(),
                           options.registered_method(),
                           options.timeout() + Timestamp::Now(),
                           options.compression_options(), event_engine().get(),
                           SimpleArenaAllocator()->MakeArena(), destination_);
    return call_;
  }

 private:
  class TestCallDestination final : public UnstartedCallDestination {
   public:
    void Orphaned() override {}
    void StartCall(UnstartedCallHandler handler) override {
      Crash("unimplemented");
    }
  };

  void InitTest() override {
    cq_ = grpc_completion_queue_create_for_next(nullptr);
  }

  void Shutdown() override {
    if (call_ != nullptr) {
      grpc_call_unref(call_);
    }
    grpc_completion_queue_shutdown(cq_);
    auto ev = grpc_completion_queue_next(
        cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    CHECK_EQ(ev.type, GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cq_);
  }

  grpc_completion_queue* cq_ = nullptr;
  grpc_call* call_ = nullptr;
  RefCountedPtr<TestCallDestination> destination_ =
      MakeRefCounted<TestCallDestination>();
};

#define CLIENT_CALL_TEST(name) YODEL_TEST(ClientCallTest, name)

CLIENT_CALL_TEST(NoOp) { InitCall(CallOptions()); }

}  // namespace grpc_core
